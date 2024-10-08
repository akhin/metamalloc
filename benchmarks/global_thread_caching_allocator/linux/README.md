RUNNING THE BENCHMARKS

	For metamalloc , run run_with_metamalloc_simple_heap_pow2.sh
	For tcmalloc , run run_with_tcmalloc.sh.sh
	For IntelOneTBB , run run_with_intelonetbb.sh
	For GNU LibC , run run_with_gnulibc_malloc.sh

BINARY VERSIONS

	INTELONETBB         2021.11.0
	TCMALLOC            2.9.1-0ubuntu3
	METAMALLOC          1.0.0 with SimpleHeapPow2

GETTING TCMALLOC

	sudo apt-get install google-perftools
	
BUILDING METAMALLOC SIMPLEHEAPPOW2

	Navigate to "examples/integration-linux_so_ld_preload" , then :
	
			chmod +x build.sh
			./build.sh

BUILDING INTELONETBB

	# Do our experiments in /tmp
	cd /tmp
	git clone https://github.com/oneapi-src/oneTBB.git
	cd oneTBB
	mkdir build && cd build
	# Configure: customize CMAKE_INSTALL_PREFIX and disable TBB_TEST to avoid tests build
	cmake -DCMAKE_INSTALL_PREFIX=/tmp/my_installed_onetbb -DTBB_TEST=OFF ..
	cmake --build .
	cmake --install .
	# Your installed oneTBB is in /tmp/my_installed_onetbb => /tmp/my_installed_onetbb/lib/libtbbmalloc_proxy.so