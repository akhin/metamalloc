#include <cstdlib>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <string>


using namespace std;

///////////////////////////////////////////////////////
class Foo
{
    public:

        Foo()
        {
            m_1 = "aaaaaaaaaaaaaaaa";

            for (int i = 0; i < 47; i++)
            {
                m_2.push_back(std::to_string(i) + "aaaa");
            }

            for (int i = 0; i < 47; i++)
            {
                m_3[i] = (std::to_string(i) + "bbbb");
            }
        }

    private:
        std::string m_1;
        std::vector<std::string> m_2;
        std::unordered_map<int, std::string> m_3;
};
///////////////////////////////////////////////////////

struct Allocation
{
    void* ptr = nullptr;
    uint16_t size_class = 0;
};

bool validate_allocation(std::size_t allocation_size, std::vector<Allocation>& allocation_vector, std::size_t repeat_count);

template <typename T>
bool validate_cpp_object_allocation(std::vector<T*>& allocation_vector);

inline bool validate_buffer(void* buffer, std::size_t buffer_size);

int main()
{
    std::cout << "\n\nSAMPLEAPP STARTING\n\n";


    std::vector<Allocation> allocations;
    std::vector<Foo*> object_allocations;

    auto thread_count = 32;
    auto allocation_per_thread_count = 256;
    auto allocation_size = 8192;

    auto allocating_thread_function = [&](unsigned int thread_id)
    {
        //pthread_setname_np(thread_id, name.data());

        for (auto i{ 0 }; i < allocation_per_thread_count; i++)
        {
            validate_allocation(allocation_size, allocations, 1);
        }

        for (auto i{ 0 }; i < allocation_per_thread_count; i++)
        {
            validate_cpp_object_allocation<Foo>(object_allocations);
        }
    };

    std::vector<std::unique_ptr<std::thread>> allocating_threads;

    for (auto i{ 0 }; i < thread_count; i++)
    {
        allocating_threads.emplace_back(new std::thread(allocating_thread_function, i));
    }

    for (auto& thread : allocating_threads)
    {
        thread->join();
    }


    std::size_t total_allocated_size{ 0 };

    for (auto& allocation : allocations)
    {
        total_allocated_size += allocation.size_class;
        free(allocation.ptr);
    }

    for (auto& object : object_allocations)
    {
        delete object;
    }

    std::size_t expected_total_allocation_size = allocation_size * allocation_per_thread_count * thread_count;

    if (total_allocated_size != expected_total_allocation_size)
    {
        cout << "Allocator failed !!!" << endl;
        return -1;
    }

    std::cout << "\n\nSAMPLEAPP ENDING\n\n";

    #if _WIN32
    std::system("pause");
    #endif

    return 0;
}

inline bool validate_buffer(void* buffer, std::size_t buffer_size)
{
    char* char_buffer = static_cast<char*>(buffer);

    // TRY WRITING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        char* dest = char_buffer + i;
        *dest = static_cast<char>(i);
    }

    // NOW CHECK READING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        auto test = char_buffer[i];
        if (test != static_cast<char>(i))
        {
            return false;
        }
    }

    return true;
}

std::mutex g_mutex; // For shared allocations vector
std::mutex g_mutex_objects; // For shared object allocations vector

bool validate_allocation(std::size_t allocation_size, std::vector<Allocation>& allocation_vector, std::size_t repeat_count)
{
    for (std::size_t i = 0; i < repeat_count; i++)
    {
        void* ptr = nullptr;
        ptr = malloc(allocation_size);

        if (ptr == nullptr)
        {
            std::cout << "ALLOCATION FAILED !!!" << std::endl;
            return false;
        }
        else
        {
            bool buffer_ok = validate_buffer(reinterpret_cast<void*>(ptr), allocation_size);

            if (buffer_ok == false)
            {
                std::cout << "BUFFER VALIDATION FAILED !!!" << std::endl;
                return false;

            }
            Allocation allocation;
            allocation.ptr = ptr;
            allocation.size_class = static_cast<uint16_t>(allocation_size);

            g_mutex.lock();
            allocation_vector.push_back(allocation);
            g_mutex.unlock();
        }
    }

    return true;
}

template <typename T>
bool validate_cpp_object_allocation(std::vector<T*>& allocation_vector)
{
    T* ptr = nullptr;
    ptr = new T;

    if (ptr == nullptr)
    {
        std::cout << "ALLOCATION FAILED !!!" << std::endl;
        return false;
    }

    g_mutex_objects.lock();
    allocation_vector.push_back(ptr);
    g_mutex_objects.unlock();

    return true;
}