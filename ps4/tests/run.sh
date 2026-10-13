#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
clang++-18 -std=c++17 -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -I. -I../src -I../third_party videoout/protocol.cpp ../src/stremio.cpp ../src/util.cpp ../src/subtitles.cpp ../src/bidi.cpp tests/protocol_test.cpp -Wl,--gc-sections -pthread -o build/tests/protocol_test
build/tests/protocol_test
clang-18 -std=c11 -O1 -g -fsanitize=address,undefined -I. videoout/video_convert.c tests/video_test.c -o build/tests/video_test
build/tests/video_test
clang-18 -std=c11 -O1 -g -fsanitize=address,undefined -Itests/mocks -I. videoout/playback.c videoout/video_convert.c tests/playback_test.c -pthread -o build/tests/playback_test
build/tests/playback_test
clang++-18 -std=c++17 -O1 -g -fsanitize=address,undefined -I../src tests/torrent_crypto_test.cpp -o build/tests/torrent_crypto_test
build/tests/torrent_crypto_test
