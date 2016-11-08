# hzr

This is a fast Huffman+RLE compression library. Its main features are:

 * Very fast compression and decompression (usually much faster than zlib).
 * Close to symmetric compression / decompression performance.
 * Optimized for stochastic data with many values close to zero.
 * Suitable for entropy reduced data (e.g. for image and audio compression).

