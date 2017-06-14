#!/bin/bash

function run-and-time {
    ./bin/stride -c $1 | grep "total simulation time:" | grep -o "0.*"
}

make clean
export STRIDE_PARALLELIZATION_LIBRARY=OpenMP
make
make install_test
pushd build/installed
echo "# small-openmp" > ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_small.xml >> ../../measurements.txt
done
echo "# medium-openmp" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_medium.xml >> ../../measurements.txt
done
echo "# large-openmp" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_large.xml >> ../../measurements.txt
done
popd

make clean
export STRIDE_PARALLELIZATION_LIBRARY=TBB
make
make install_test
pushd build/installed
echo "# small-tbb" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_small.xml >> ../../measurements.txt
done
echo "# medium-tbb" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_medium.xml >> ../../measurements.txt
done
echo "# large-tbb" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_large.xml >> ../../measurements.txt
done
popd

make clean
export STRIDE_PARALLELIZATION_LIBRARY=STL
make
make install_test
pushd build/installed
echo "# small-stl" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_small.xml >> ../../measurements.txt
done
echo "# medium-stl" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_medium.xml >> ../../measurements.txt
done
echo "# large-stl" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_large.xml >> ../../measurements.txt
done
popd

make clean
export STRIDE_PARALLELIZATION_LIBRARY=none
make
make install_test
pushd build/installed
echo "# small-none" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_small.xml >> ../../measurements.txt
done
echo "# medium-none" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_medium.xml >> ../../measurements.txt
done
echo "# large-none" >> ../../measurements.txt
for i in `seq 1 10`;
do
    run-and-time config/run_popgen_large.xml >> ../../measurements.txt
done
popd
