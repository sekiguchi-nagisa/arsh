mkdir -p build-all
cd build-all
cmake .. -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DUSE_EXTRA_TEST=on

ninja

./ydsh ../tools/scripts/copy_mod4extra.ds

exec ctest --output-on-failure
