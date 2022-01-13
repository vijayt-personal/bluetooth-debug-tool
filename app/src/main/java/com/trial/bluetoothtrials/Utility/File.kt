package com.trial.bluetoothtrials.Utility

import android.content.Context
import android.os.Environment
import android.util.Log
import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.util.*
import kotlin.experimental.and
import kotlin.experimental.xor

class File private constructor(inputStream: InputStream) {
    private val DEFAULT_FILE_CHUNK_SIZE: Int=20
    private val inputStream: InputStream?
    var crc: Byte = 0
        private set
    private lateinit var bytes: ByteArray
    private lateinit var blocks: Array<Array<ByteArray?>?>
    var fileBlockSize = 0
        private set
    private var fileChunkSize: Int = DEFAULT_FILE_CHUNK_SIZE
    private val bytesAvailable: Int
    var numberOfBlocks = -1
        private set
    var chunksPerBlockCount = 0
        private set
    var totalChunkCount = 0
        private set
    private var type = 0
    @Throws(IOException::class)
    fun setType(type: Int) {
        this.type = type
//        if (type == SuotaManager.TYPE) {
//            // Reserve 1 extra byte to the total array for the CRC code
//            bytes = ByteArray(bytesAvailable + 1)
//            inputStream!!.read(bytes)
//            crc = calculateCrc()
//            bytes[bytesAvailable] = crc
//        } else {

//        }
    }

    fun getNumberOfBytes(): Int {
        return bytes.size
    }

    fun setFileBlockSize(fileBlockSize: Int, fileChunkSize: Int) {
        bytes = ByteArray(bytesAvailable)
//        inputStream!!.read(bytes)
        this.fileBlockSize = Math.max(fileBlockSize, fileChunkSize)
        this.fileChunkSize = fileChunkSize
        if (this.fileBlockSize > bytes.size) {
            this.fileBlockSize = bytes.size
            if (this.fileChunkSize > this.fileBlockSize) this.fileChunkSize = this.fileBlockSize
        }
        chunksPerBlockCount = this.fileBlockSize / this.fileChunkSize + if (this.fileBlockSize % this.fileChunkSize != 0) 1 else 0
        numberOfBlocks = bytes.size / this.fileBlockSize + if (bytes.size % this.fileBlockSize != 0) 1 else 0
        initBlocks()
    }

    private fun initBlocksSuota() {
        totalChunkCount = 0
        blocks = arrayOfNulls(numberOfBlocks)
        var byteOffset = 0
        // Loop through all the bytes and split them into pieces the size of the default chunk size
        for (i in 0 until numberOfBlocks) {
            var blockSize = fileBlockSize
            var numberOfChunksInBlock = chunksPerBlockCount
            // Check if the last block needs to be smaller
            if (byteOffset + fileBlockSize > bytes.size) {
                blockSize = bytes.size % fileBlockSize
                numberOfChunksInBlock = blockSize / fileChunkSize + if (blockSize % fileChunkSize != 0) 1 else 0
            }
            var chunkNumber = 0
            blocks[i] = arrayOfNulls(numberOfChunksInBlock)
            var j = 0
            while (j < blockSize) {

                // Default chunk size
                var chunkSize = fileChunkSize
                // Last chunk in block
                if (j + fileChunkSize > blockSize) {
                    chunkSize = blockSize % fileChunkSize
                }

                //Log.d("chunk", "total bytes: " + bytes.length + ", offset: " + byteOffset + ", block: " + i + ", chunk: " + (chunkNumber + 1) + ", blocksize: " + blockSize + ", chunksize: " + chunkSize);
                val chunk = Arrays.copyOfRange(bytes, byteOffset, byteOffset + chunkSize)
                blocks[i]?.set(chunkNumber, chunk)
                byteOffset += chunkSize
                chunkNumber++
                totalChunkCount++
                j += fileChunkSize
            }
        }
    }

    private fun initBlocksSpota() {

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
//        if (type == SuotaManager.TYPE) {
//            initBlocksSuota()
//        } else if (type == SpotaManager.TYPE) {
            initBlocksSpota()
//        }
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

    @Throws(IOException::class)
    private fun calculateCrc(): Byte {
        var crc_code: Byte = 0
        for (i in 0 until bytesAvailable) {
            val byteValue = bytes[i]
            val intVal = byteValue.toInt()
            crc_code = crc_code xor intVal.toByte()
        }
        Log.d("crc", String.format("Fimware CRC: %#04x", crc_code and 0xff.toByte()))
        return crc_code
    }

    companion object {
        private val filesDir = Environment.getExternalStorageDirectory().absolutePath + "/Suota"
        @Throws(IOException::class)
        fun getByFileName(filename: String): File {
            // Get the file and store it in fileStream
            val `is`: InputStream = FileInputStream(filename)
            return File(`is`)
        }
//
//        fun list(): ArrayList<String>? {
//            val f = java.io.File(filesDir)
//            val file = f.listFiles() ?: return null
//            Arrays.sort(file, object : Comparator<java.io.File?> {
//
//                override fun compare(o1: java.io.File?, o2: java.io.File?): Int {
//                    if (o1 != null) {
//                        if (o2 != null) {
//                            return o1.path.compareTo(o2.path, ignoreCase = true)
//                        }
//                    }
//                }
//            })
//            Log.d("Files", "Size: " + file.size)
//            val names = ArrayList<String>()
//            for (i in file.indices) {
//                Log.d("Files", "FileName: " + file[i].name)
//                names.add(file[i].name)
//            }
//            return names
//        }

        fun createFileDirectories(c: Context?): Boolean {
            val directoryName = filesDir
            val directory: java.io.File
            directory = java.io.File(directoryName)
            return directory.exists() || directory.mkdirs()
        }
    }

    init {
        this.inputStream = inputStream
        bytesAvailable = this.inputStream.available()
    }
}
