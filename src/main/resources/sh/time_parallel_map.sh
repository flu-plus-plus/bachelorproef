#!/bin/bash
function run-and-time {
    ./gtester --gtest_filter="*$1" | grep "OK" | grep -o "(.*)" | sed "s/(\(.*\) ms)/0:0:0:\1:0/g"
}

make install_test
pushd build/installed/bin
echo "# serial-map1" > ../../../parallel_map_measurements.txt
for i in `seq 1 50`;
do
    run-and-time MapPerf1Serial >> ../../../parallel_map_measurements.txt
done
echo "# parallel-map1" >> ../../../parallel_map_measurements.txt
for i in `seq 1 50`;
do
    run-and-time MapPerf1Parallel >> ../../../parallel_map_measurements.txt
done
echo "# serial-map2" >> ../../../parallel_map_measurements.txt
for i in `seq 1 50`;
do
    run-and-time MapPerf2Serial >> ../../../parallel_map_measurements.txt
done
echo "# parallel-map2" >> ../../../parallel_map_measurements.txt
for i in `seq 1 50`;
do
    run-and-time MapPerf2Parallel >> ../../../parallel_map_measurements.txt
done
echo "# serial-map3" >> ../../../parallel_map_measurements.txt
for i in `seq 1 50`;
do
    run-and-time MapPerf3Serial >> ../../../parallel_map_measurements.txt
done
echo "# parallel-map3" >> ../../../parallel_map_measurements.txt
for i in `seq 1 50`;
do
    run-and-time MapPerf3Parallel >> ../../../parallel_map_measurements.txt
done
popd