## 빌드
# cd ..
# rm -rf ./_skbuild ./dist
# pip wheel . -w dist/ -v
CMAKE_BUILD_PARALLEL_LEVEL=3 pip wheel . -w dist/ -v
# CMAKE_BUILD_PARALLEL_LEVEL=3 pip wheel . -w dist/ -v --no-cache-dir 
