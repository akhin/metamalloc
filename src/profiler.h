/*
    UI LOOP

        1. HTTPLiveProfiler which is an HTTP reactor, serves the page
        2. Browser side sends a post request
        3. HttpLiveProfiler responds to that post request with csv data
        4. Browser side updates UI based on that CSV data
*/
#ifndef __PROFILER_H__
#define __PROFILER_H__

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <mutex> // For std::lock_guard
#include <malloc.h> // For malloc_usable_size and _msize
#include <new> // std::get_new_handler

#include "compiler/builtin_functions.h"
#include "compiler/unused.h"
#include "os/thread_local_storage.h"
#include "utilities/userspace_spinlock.h"
#include "utilities/http_reactor.h"
#include "utilities/log2_utilities.h"
#include "utilities/pow2_utilities.h"

/////////////////////////////////////////////////////////////
// PROFILER
static constexpr std::size_t MAX_SIZE_CLASS_COUNT = 20; // Up to 1 MB

struct SizeClassStats
{
    std::size_t m_alloc_count = 0;
    std::size_t m_free_count = 0;
    std::size_t m_peak_usage_count = 0;
    std::size_t m_usage_count = 0;


    void add_allocation()
    {
        m_usage_count++;

        if (m_usage_count > m_peak_usage_count)
        {
            m_peak_usage_count = m_usage_count;
        }

        m_alloc_count++;
    }

    void add_deallocation()
    {
        m_usage_count--;
        m_free_count++;
    }

    void reset()
    {
        m_alloc_count = 0;
        m_free_count = 0;
        m_peak_usage_count = 0;
        m_usage_count = 0;
    }
};

struct ThreadStats
{
    std::size_t m_tls_id = 0;
    std::array<SizeClassStats, MAX_SIZE_CLASS_COUNT> m_size_class_stats;
};

class Profiler
{
    public:

        static Profiler& get_instance()
        {
            static Profiler instance;
            return instance;
        }

        static constexpr inline std::size_t MAX_THREAD_COUNT = 64;

        void capture_allocation(void* address, std::size_t size)
        {
            if(size == 0 ) return ;

            auto index = find_stats_index_from_size(size);

            if (index >= MAX_SIZE_CLASS_COUNT) return;

            ThreadStats* thread_local_stats = get_thread_local_stats();

            if (thread_local_stats == nullptr) return; // MAX_THREAD_COUNT was not enough

            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            (*thread_local_stats).m_size_class_stats[index].add_allocation();
        }

        void capture_deallocation(void* address)
        {
            if(address == nullptr) return;

            std::size_t original_allocation_size = 0;

            #ifdef __linux__
            original_allocation_size = static_cast<std::size_t>(malloc_usable_size(address));
            #elif _WIN32
            original_allocation_size = static_cast<std::size_t>(_msize(address));
            #endif

            auto index = find_stats_index_from_size(original_allocation_size);

            if (index >= MAX_SIZE_CLASS_COUNT) return;

            ThreadStats* thread_local_stats = get_thread_local_stats();

            if (thread_local_stats == nullptr) return; // MAX_THREAD_COUNT was not enough

            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            (*thread_local_stats).m_size_class_stats[index].add_deallocation();
        }

        void reset_stats()
        {
            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            for(std::size_t i=0; i<MAX_THREAD_COUNT; i++)
            {
                for(auto stats : m_stats[i].m_size_class_stats)
                {
                    stats.reset();
                }
            }
        }

        ThreadStats* get_stats() { return &m_stats[0]; }
        std::size_t get_observed_thread_count() { return m_observed_thread_count; }

    private:
        UserspaceSpinlock<> m_lock;
        std::size_t m_observed_thread_count = 0;
        ThreadStats m_stats[MAX_THREAD_COUNT];

        Profiler()
        {
            m_lock.initialise();
            ThreadLocalStorage::get_instance().create();
        }

        ~Profiler()
        {
            ThreadLocalStorage::get_instance().destroy();
        }

        Profiler(const Profiler& other) = delete;
        Profiler& operator= (const Profiler& other) = delete;
        Profiler(Profiler&& other) = delete;
        Profiler& operator=(Profiler&& other) = delete;

        ThreadStats* get_thread_local_stats()
        {
            ThreadStats* stats = reinterpret_cast<ThreadStats*>(ThreadLocalStorage::get_instance().get());

            if (stats == nullptr)
            {
                std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

                if (m_observed_thread_count != MAX_THREAD_COUNT)
                {
                    m_stats[m_observed_thread_count].m_tls_id = ThreadLocalStorage::get_thread_local_storage_id();
                    stats = &(m_stats[m_observed_thread_count]);
                    ThreadLocalStorage::get_instance().set(stats);
                    m_observed_thread_count++;
                }
            }

            return stats;
        }

        std::size_t find_stats_index_from_size(std::size_t size)
        {
            auto index = Pow2Utilities::get_first_pow2_of(size);
            index = Log2Utilities::log2(index);
            return index;
        }
};

/////////////////////////////////////////////////////////////
// PROFILER PROXIES
void* proxy_malloc(std::size_t size)
{
    void* ret = malloc(size);
    Profiler::get_instance().capture_allocation(ret, size);
    return ret;
}

void* proxy_calloc(std::size_t count, std::size_t size)
{
    void * ret =  calloc(count, size);
    Profiler::get_instance().capture_allocation(ret, count*size);
    return ret;
}

void* proxy_realloc(void* address, std::size_t size)
{
    void* ret= realloc(address, size);
    Profiler::get_instance().capture_allocation(address, size);
    return ret;
}

void proxy_free(void* address)
{
    Profiler::get_instance().capture_deallocation(address);
    free(address);
}

void proxy_aligned_free(void* address)
{
    Profiler::get_instance().capture_deallocation(address);
    builtin_aligned_free(address);
}

void* proxy_aligned_alloc(std::size_t size, std::size_t alignment)
{
    void * ret = builtin_aligned_alloc(size, alignment);
    Profiler::get_instance().capture_allocation(ret, size);
    return ret;
}

static Lock g_new_handler_lock;

void handle_operator_new_failure()
{
    std::new_handler handler;

    // std::get_new_handler is not thread safe
    g_new_handler_lock.lock();
    handler = std::get_new_handler();
    g_new_handler_lock.unlock();

    if(handler != nullptr)
    {
        handler();
    }
    else
    {
        throw std::bad_alloc();
    }
}

void* proxy_operator_new(std::size_t size)
{
    void* ret = proxy_malloc(size);

    if( ret==nullptr )
    {
        handle_operator_new_failure();
    }

    return ret;
}

void* proxy_operator_new_aligned(std::size_t size, std::size_t alignment)
{
    void* ret = proxy_aligned_alloc(size, alignment);

    if( ret==nullptr )
    {
        handle_operator_new_failure();
    }

    return ret;
}

/////////////////////////////////////////////////////////////
// PROFILER REDIRECTIONS

#define malloc(size) proxy_malloc(size)
#define calloc(count, size) proxy_calloc(count, size)
#define realloc(address,size) proxy_realloc(address,size)
#define free(address) proxy_free(address)

#if __linux__
#define aligned_alloc(alignment, size) proxy_aligned_alloc(size, alignment)
#endif

#if _WIN32
#define _aligned_malloc(size, alignment) proxy_aligned_alloc(size, alignment)
#define _aligned_free(ptr) proxy_aligned_free(ptr)
#endif

/////////////////////////////////////////////////////////////
// PROFILER OPERATOR NEW DELETE OVERLOADS

// VOLTRON_NAMESPACE_EXCLUSION_START

// USUAL OVERLOADS
void* operator new(std::size_t size)
{
    return memlive::proxy_operator_new(size);
}

void operator delete(void* ptr) noexcept
{
    memlive::proxy_free(ptr);
}

void* operator new[](std::size_t size)
{
    return memlive::proxy_operator_new(size);
}

void operator delete[](void* ptr) noexcept
{
    memlive::proxy_free(ptr);
}

// WITH ALIGNMENT
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return memlive::proxy_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
    memlive::proxy_free(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return memlive::proxy_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
    memlive::proxy_free(ptr);
}

// WITH std::nothrow_t
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return memlive::proxy_malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    memlive::proxy_free(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return memlive::proxy_malloc(size);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    memlive::proxy_free(ptr);
}

// WITH ALIGNMENT and std::nothrow_t

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    return memlive::proxy_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    return memlive::proxy_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    memlive::proxy_aligned_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    memlive::proxy_aligned_free(ptr);
}

// DELETES WITH SIZES
void operator delete(void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    memlive::proxy_free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    memlive::proxy_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    memlive::proxy_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    memlive::proxy_free(ptr);
}

// VOLTRON_NAMESPACE_EXCLUSION_END

/////////////////////////////////////////////////////////////
// MINIFIED HTML-JAVASCRIPT SOURCE

static const char* HTTPLiveProfilerHtmlSource = R"(
<!DOCTYPE html><html><head><title>memlive 1.0.0</title><style>.header {display: flex;}.header div {margin-right: 10px;}.dark-mode {background-color: #333;color: #fff;}.dark-mode th {background-color: #444;color: #fff;}#poll_interval_notification {display: none;position: fixed;bottom: 10px;left: 10px;background-color: #4CAF50;color: #fff;padding: 15px;border-radius: 5px;box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);}canvas {border: 1px solid #000;}</style></head><body><div id="poll_interval_notification"></div><div class="header"><div>   </div><label for="darkModeCheckbox" style="display: inline;">Dark Mode</label><input type="checkbox" id="darkModeCheckbox"><div>   </div><div><label for="pollingInterval">Polling interval ( milliseconds ):</label><input type="text" id="pollingInterval" value="0" style="width: 30px;"><button id="pollingIntervalButton">Apply polling interval</button></div></div><canvas id="areaChart" width="570" height="60"></canvas><div id="parent-container" style="display: flex; flex-direction: row;"><div id="combobox-container"><label>Threads</label></div><div id="table-container" style="float: right;"></div></div><script>const darkModeCheckbox = document.getElementById("darkModeCheckbox");let darkMode = true;let uiCreated = false;let pollingTimer;let csvData;let csvDataTokens;let statsNumberPerSizeClass = 3;let currentUsage = 0;let chartDataMaxWidth = 50;let chartScale = 0; let chartData = [];const chartAreaColourDarkMode = 'rgba(0, 128, 255, 0.9)';const chartAreaColourLightMode = 'rgba(0, 128, 255, 0.3)';let chartAreaColour = chartAreaColourDarkMode;const chartTextColourDarkMode = 'white';const chartTextColourLightMode = 'black';let chartTextColour = chartTextColourDarkMode;function getHumanReadibleSizeString(bytes){const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];if (bytes === 0) return '0 Byte';const i = parseInt(Math.floor(Math.log(bytes) / Math.log(1024)));return Math.round(100 * (bytes / Math.pow(1024, i))) / 100 + ' ' + sizes[i];}function postRequest(){var xhr = new XMLHttpRequest();xhr.open("POST", window.location.href, true);xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");var data = "memlive-request";xhr.onreadystatechange = function () {if (xhr.readyState === 4) {if (xhr.status === 201){csvData = xhr.responseText;csvDataTokens = csvData.split(',');if(uiCreated == false){createUI();uiCreated=true;}updateTable();updateCurrentUsage();drawChart("Current usage" + ': ' + getHumanReadibleSizeString(chartData[chartDataMaxWidth - 1]));setupPollingTimer();}}};xhr.send(data);}function setupPollingTimer(){if (pollingTimer){clearTimeout(pollingTimer);}const newInterval = parseInt(document.getElementById('pollingInterval').value);pollingTimer = setTimeout(postRequest, newInterval);}function set_poll_interval_notification(){var notification = document.getElementById("poll_interval_notification");notification.innerHTML = "Successfully set the polling interval to " + parseInt(document.getElementById('pollingInterval').value).toString() + " milliseconds.";notification.style.display = "block";setTimeout(function(){notification.style.display = "none";}, 3000);}function updateChartScale(){if (chartData.length > 0){const maxDataValue = Math.max(...chartData);const powerOf10 = Math.pow(10, Math.floor(Math.log10(maxDataValue)));chartScale = Math.ceil(maxDataValue / powerOf10) * powerOf10;}else{chartScale = 100;}}function updateChartData(newValue){chartData.push(newValue);if (chartData.length > chartDataMaxWidth){chartData.shift();}updateChartScale();}function drawChart(chartTopLeftText){const canvas = document.getElementById('areaChart');const ctx = canvas.getContext('2d');const width = canvas.width;const height = canvas.height;ctx.clearRect(0, 0, width, height);ctx.strokeStyle = 'rgba(0, 0, 0, 0.8)';ctx.lineWidth = 0.5;const stepX = width / (chartDataMaxWidth - 1);const stepY = height / 5;for (let i = 0; i < chartDataMaxWidth; i++){const x = i * stepX;ctx.beginPath();ctx.moveTo(x, 0);ctx.lineTo(x, height);ctx.stroke();}for (let i = 0; i <= 5; i++){const y = i * stepY;ctx.beginPath();ctx.moveTo(0, y);ctx.lineTo(width, y);ctx.stroke();}ctx.fillStyle = chartAreaColour;ctx.beginPath();ctx.moveTo(0, height);for (let i = 0; i < chartDataMaxWidth; i++){const x = i * stepX;const y = height - (chartData[i] / chartScale) * height;ctx.lineTo(x, y);}ctx.lineTo(width, height);ctx.closePath();ctx.fill();ctx.fillStyle = chartTextColour;ctx.font = '14px Arial';const textPosX = 10;const textPosY = 20;ctx.fillText(chartTopLeftText, textPosX, textPosY);}function getThreadCount(){return csvDataTokens[0];}function getSizeClassCount(){return csvDataTokens[1];}function getAllocationCount(selectedThreadIndex, sizeclassIndex){targetTokenIndex = 2 + (selectedThreadIndex * getSizeClassCount() * statsNumberPerSizeClass ) + (sizeclassIndex*statsNumberPerSizeClass);return csvDataTokens[targetTokenIndex];}function getDeallocationCount(selectedThreadIndex, sizeclassIndex){targetTokenIndex = 2 + (selectedThreadIndex * getSizeClassCount() * statsNumberPerSizeClass ) + (sizeclassIndex*statsNumberPerSizeClass) + 1;return csvDataTokens[targetTokenIndex];}function getPeakUsageCount(selectedThreadIndex, sizeclassIndex){targetTokenIndex = 2 + (selectedThreadIndex * getSizeClassCount() * statsNumberPerSizeClass ) + (sizeclassIndex*statsNumberPerSizeClass) + 2;return csvDataTokens[targetTokenIndex];}function getTotalAllocationCountForSizeClass(sizeclassIndex){ret=0;threadCount = getThreadCount();for (let i = 0; i < threadCount; i++){ret += parseInt(getAllocationCount(i, sizeclassIndex));}return ret;}function getTotalDeallocationCountForSizeClass(sizeclassIndex){ret=0;threadCount = getThreadCount();for (let i = 0; i < threadCount; i++){ret += parseInt(getDeallocationCount(i, sizeclassIndex));}return ret;}function getTotalPeakUsageCountForSizeClass(sizeclassIndex){ret=0;threadCount = getThreadCount();for (let i = 0; i < threadCount; i++){ret += parseInt(getPeakUsageCount(i, sizeclassIndex));}return ret;}function getOverallPeakUsageInBytes(){ret=0;sizeclassCount = getSizeClassCount();for (let i = 0; i < sizeclassCount; i++){ret += parseInt(getTotalPeakUsageCountForSizeClass(i)) * Math.pow(2, i+1);}return ret;}function createUI(){threadCount = getThreadCount();const comboBox = document.createElement('select');comboBox.id = 'threadsCombobox';for (let i = 0; i < threadCount; i += 1){const option = document.createElement('option');option.text = "Thread " + (i+1).toString();comboBox.add(option);}const option = document.createElement('option');option.text = "Total";comboBox.add(option);const container = document.getElementById('combobox-container');container.appendChild(comboBox);const table = document.createElement('table');table.id = 'dataTable';table.border = '1';const tableContainer = document.getElementById('table-container');tableContainer.innerHTML = '';tableContainer.appendChild(table);comboBox.addEventListener('change', function(){updateTable();});}function updateCurrentUsage(){calculatedUsage = 0;threadCount = getThreadCount();sizeclassCount = getSizeClassCount();for (let i = 0; i < threadCount; i++){for (let j = 0; j < sizeclassCount; j++){currentAllocationCount = getAllocationCount(i, j);currentDeallocationCount = getDeallocationCount(i, j);if(currentAllocationCount>currentDeallocationCount){calculatedUsage += ((currentAllocationCount-currentDeallocationCount) * Math.pow(2, j+1));}}}currentUsage = calculatedUsage;updateChartData(currentUsage);}function updateTable(){const table = document.getElementById('dataTable');threadCount = getThreadCount();sizeclassCount = getSizeClassCount();const combobox = document.getElementById('threadsCombobox');const selectedIndex = combobox.selectedIndex;if(selectedIndex >= 0 ){table.innerHTML = '';const newRow = table.insertRow();const sizeClassCell = newRow.insertCell(0);const allocationCountCell = newRow.insertCell(1);const deallocationCountCell = newRow.insertCell(2);const peakUsageCell = newRow.insertCell(3);sizeClassCell.textContent = 'Size Class';allocationCountCell.textContent = 'Allocation Count';deallocationCountCell.textContent = 'Deallocation Count';peakUsageCell.textContent = 'Peak Usage in bytes';for (let i = 0; i < sizeclassCount; i++){const newRow = table.insertRow();const sizeClassCell = newRow.insertCell(0);const allocationCountCell = newRow.insertCell(1);const deallocationCountCell = newRow.insertCell(2);const peakUsageCell = newRow.insertCell(3);const sizeClass = Math.pow(2, i+1);allocationCount = 0;deallocationCount = 0;peakUsageCount = 0;if(selectedIndex < threadCount ){allocationCount = getAllocationCount(selectedIndex, i);deallocationCount = getDeallocationCount(selectedIndex, i);peakUsageCount = getPeakUsageCount(selectedIndex, i);}else {allocationCount = getTotalAllocationCountForSizeClass(i);deallocationCount = getTotalDeallocationCountForSizeClass(i);peakUsageCount = getTotalPeakUsageCountForSizeClass(i);}sizeClassCell.textContent = getHumanReadibleSizeString(sizeClass);allocationCountCell.textContent = allocationCount;deallocationCountCell.textContent = deallocationCount;peakUsageCell.textContent = getHumanReadibleSizeString(sizeClass*peakUsageCount);}if(selectedIndex == threadCount ){const newRow = table.insertRow();const sizeClassCell = newRow.insertCell(0);const allocationCountCell = newRow.insertCell(1);const deallocationCountCell = newRow.insertCell(2);const peakUsageCell = newRow.insertCell(3);sizeClassCell.textContent = 'SUM';allocationCountCell.textContent = 'N/A';deallocationCountCell.textContent = 'N/A';peakUsageCell.textContent = getHumanReadibleSizeString(getOverallPeakUsageInBytes());}}}document.getElementById('pollingInterval').value = 1000;darkModeCheckbox.addEventListener("change", function(){if (darkModeCheckbox.checked){darkMode = true;document.body.classList.add("dark-mode");chartAreaColour = chartAreaColourDarkMode;chartTextColour = chartTextColourDarkMode;}else{darkMode = false;document.body.classList.remove("dark-mode");chartAreaColour = chartAreaColourLightMode;chartTextColour = chartTextColourLightMode;}});if(darkMode){document.body.classList.add("dark-mode");darkModeCheckbox.checked = true;}else{document.body.classList.remove("dark-mode");darkModeCheckbox.checked = false;}for (let i = 0; i < chartDataMaxWidth; i++){updateChartData(0);}document.getElementById('pollingIntervalButton').addEventListener('input', setupPollingTimer);document.getElementById('pollingIntervalButton').addEventListener('click', set_poll_interval_notification);setupPollingTimer();</script></body></html>
)";

/////////////////////////////////////////////////////////////
// HTTP LIVE PROFILER

class HTTPLiveProfiler : public HTTPReactor<HTTPLiveProfiler>
{
    public:

        HTTPLiveProfiler()
        {
            // WE IMPLEMENT POST REQUESTS USING AJAX'S XMLHttpRequest. THEREFORE THERE IS NO WAY TO REUSE AN EXISTING TCP CONNECTION
            // https://stackoverflow.com/questions/32505128/how-to-make-xmlhttprequest-reuse-a-tcp-connection
            this->m_connection_per_request = true;
        }

        ~HTTPLiveProfiler() = default;
        HTTPLiveProfiler(const HTTPLiveProfiler& other) = delete;
        HTTPLiveProfiler& operator= (const HTTPLiveProfiler& other) = delete;
        HTTPLiveProfiler(HTTPLiveProfiler&& other) = delete;
        HTTPLiveProfiler& operator=(HTTPLiveProfiler&& other) = delete;

        void on_http_get_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            HttpResponse response;
            response.set_response_code_with_text("200 OK");
            response.set_connection_alive(false);

            response.set_body(HTTPLiveProfilerHtmlSource);

            auto response_text = response.get_as_text();
            connector_socket->send(response_text.c_str());
        }

        void on_http_post_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            HttpResponse response;
            response.set_response_code_with_text("201 Created");
            response.set_connection_alive(false);

            update_post_response();
            response.set_body(m_post_response_buffer);

            auto response_text = response.get_as_text();
            connector_socket->send(response_text.c_str());
        }

        void on_http_put_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            UNUSED(http_request);
            UNUSED(connector_socket);
        }

        void on_http_delete_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            UNUSED(http_request);
            UNUSED(connector_socket);
        }

    private:
        static constexpr inline std::size_t MAX_POST_RESPONSE_BUFFER_SIZE = 4096;
        static constexpr inline std::size_t MAX_POST_RESPONSE_DIGITS = 16;
        char m_post_response_buffer[MAX_POST_RESPONSE_BUFFER_SIZE] = {(char)0};

        void update_post_response()
        {
            std::size_t thread_count = Profiler::get_instance().get_observed_thread_count();
            std::size_t size_class_count = MAX_SIZE_CLASS_COUNT;

            auto stats = Profiler::get_instance().get_stats();

            snprintf(m_post_response_buffer, sizeof(m_post_response_buffer), "%zd,%zd,", thread_count-1, size_class_count);  // -1 as we are excluding the reactor thread

            for (std::size_t i = 0; i < thread_count; i++)
            {
                if (stats[i].m_tls_id != ThreadLocalStorage::get_thread_local_storage_id()) // We are excluding the reactor thread
                {
                    for (std::size_t j = 0; j < size_class_count; j++)
                    {
                        snprintf(m_post_response_buffer + strlen(m_post_response_buffer), MAX_POST_RESPONSE_DIGITS, "%zd,", stats[i].m_size_class_stats[j].m_alloc_count);
                        snprintf(m_post_response_buffer + strlen(m_post_response_buffer), MAX_POST_RESPONSE_DIGITS, "%zd,", stats[i].m_size_class_stats[j].m_free_count);
                        snprintf(m_post_response_buffer + strlen(m_post_response_buffer), MAX_POST_RESPONSE_DIGITS, "%zd,", stats[i].m_size_class_stats[j].m_peak_usage_count);
                    }
                }
            }
        }
};

/////////////////////////////////////////////////////////////
// MEMLIVE INTERFACE
static HTTPLiveProfiler g_http_live_profiler;

void start(const std::string& address, int port)
{
    g_http_live_profiler.start(address, port);
}

void reset()
{
    Profiler::get_instance().reset_stats();
}

#endif