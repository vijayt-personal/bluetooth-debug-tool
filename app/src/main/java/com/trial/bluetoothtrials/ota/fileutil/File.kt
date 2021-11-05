package com.trial.bluetoothtrials.Utility

import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.util.*


class File private constructor(inputStream: InputStream) {
    private val DEFAULT_FILE_CHUNK_SIZE: Int=20
    private val inputStream: InputStream?
    private lateinit var bytes: ByteArray
    private lateinit var blocks: Array<Array<ByteArray?>?>
    var fileBlockSize = 0
        private set
    private var fileChunkSize: Int = DEFAULT_FILE_CHUNK_SIZE
    private val bytesAvailable: Int
    var numberOfBlocks = -1
        private set
    var totalChunkCount = 0
        private set

    fun setFileBlockSize(fileBlockSize: Int, fileChunkSize: Int) {
        bytes = ByteArray(bytesAvailable)
        inputStream!!.read(bytes)
        this.fileBlockSize = Math.max(fileBlockSize, fileChunkSize)
        this.fileChunkSize = fileChunkSize
        if (this.fileBlockSize > bytes.size) {
            this.fileBlockSize = bytes.size
            if (this.fileChunkSize > this.fileBlockSize) this.fileChunkSize = this.fileBlockSize
        }
        numberOfBlocks = bytes.size / this.fileBlockSize + if (bytes.size % this.fileBlockSize != 0) 1 else 0
        initBlocks()
    }

    private fun initBlocksOta() {
        numberOfBlocks = 1
        fileBlockSize = bytes.size
        totalChunkCount = bytes.size / fileChunkSize + if (bytes.size % fileChunkSize != 0) 1 else 0
        blocks = Array(numberOfBlocks) { arrayOfNulls(totalChunkCount) }
        var byteOffset = 0
        var chunkSize = fileChunkSize
        for (i in 0 until totalChunkCount) {
            if (byteOffset + fileChunkSize > bytes.size) {
                chunkSize = bytes.size - byteOffset
            }
            val chunk = Arrays.copyOfRange(bytes, byteOffset, byteOffset + chunkSize)
            blocks[0]?.set(i, chunk)
            byteOffset += fileChunkSize
        }
    }

    // Create the array of blocks using the given block size.
    private fun initBlocks() {
            initBlocksOta()
    }

    fun getBlock(index: Int): Array<ByteArray?>? {
        return blocks[index]
    }

    fun close() {
        if (inputStream != null) {
            try {
                inputStream.close()
            } catch (e: IOException) {
                e.printStackTrace()
            }
        }
    }

    companion object {
        @Throws(IOException::class)
        fun getByFileName(filename: String): File {
            // Get the file and store it in fileStream
            val `is`: InputStream = FileInputStream(filename)
            return File(`is`)
        }

    }

    init {
        this.inputStream = inputStream
        bytesAvailable = this.inputStream.available()
    }
}
