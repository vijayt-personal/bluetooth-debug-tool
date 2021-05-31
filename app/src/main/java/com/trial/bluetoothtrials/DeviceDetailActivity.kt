package com.trial.bluetoothtrials

import android.bluetooth.*
import android.content.Context
import android.content.Intent
import android.os.*
import android.util.Log
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.trial.bluetoothtrials.Utility.File
import com.trial.bluetoothtrials.Utility.FileChooser
import com.trial.bluetoothtrials.Utility.LoadingUtils
import com.trial.bluetoothtrials.Utility.PreferenceController
import kotlinx.android.synthetic.main.activity_device_detail.*
import kotlinx.android.synthetic.main.progress_layout.*
import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.StandardCharsets
import java.util.*


class DeviceDetailActivity : AppCompatActivity() {
    private lateinit var mHandler: Handler
    private var count:Int =0
    var three: Int=0
    var one: Char='0'
    var two: Char='0'
    var four: Int=0
    private var otaDoneFlag: Boolean=false
    lateinit var otaUpdateFlag: BluetoothGattCharacteristic
    lateinit var selectedCharacteristic: BluetoothGattCharacteristic
    lateinit var file: File
    lateinit var gatt: BluetoothGatt
    private var deviceId: String=""
    var chunkCounter = -1
    var blockCounter:Int = 0
    var lastBlockSent = false
    var lastBlock = false
    val input = byteArrayOf(
            0x45,
            0x07,
            0xb6.toByte(),
            0xf3.toByte(),
            0x16,
            0x9a.toByte(),
            0xe7.toByte(),
            0x93.toByte(),
            0x7d,
            0x3d,
            0x4b,
            0x8a.toByte(),
            0x31,
            0x70,
            0x49,
            0x8f.toByte()
    )
    var readCharacteristics=arrayListOf<BluetoothGattCharacteristic>()
    init {
        System.loadLibrary("native-lib")
    }


    var obuf = ByteArray(4)
    var dbuf = ByteArray(4)
    private val mGattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val intentAction: String
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d("TAG", "Connected to GATT server.")
                // Attempts to discover services after successful connection.
                if (Build.VERSION.SDK_INT >= 21)
                    gatt.requestMtu(251)

            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i("TAG", "Disconnected from GATT server.")

                LoadingUtils.hideDialog()
                runOnUiThread {
                    if(otaDoneFlag) {

                        Toast.makeText(applicationContext, "Update Completed", Toast.LENGTH_SHORT).show()
                    }else {
                        Toast.makeText(applicationContext, "The device connection was lost", Toast.LENGTH_SHORT).show()
                    }
                    }

                finish()
//                val also = object : Handler(Looper.getMainLooper()) {
//
//                }.also { mHandler = it }
            }
        }


        override fun onMtuChanged(gatt: BluetoothGatt?, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.e("TAG", "MTU request Success, status=${status.toString()}")
            } else {
                Log.e("TAG", "MTU request failure, status=${status.toString()}")
            }
            Log.d("TAG", "Attempting to start service discovery:" + gatt!!.discoverServices())

        }
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val services: List<BluetoothGattService> = gatt.getServices()
                for (service in services) {
                    if(service.uuid.toString().equals("e0e0e0e0-e0e0-e0e0-e0e0-e0e0e0e0e0e0")){
                        for(characteristic in service.characteristics){
                            if(characteristic.uuid.toString().equals("e5e5e5e5-e5e5-e5e5-e5e5-e5e5e5e5e5e5")){
                                selectedCharacteristic=characteristic
                                Log.d("Read", characteristic.uuid.toString())
                            }
                            if(characteristic.uuid.toString().equals("e2e2e2e2-e2e2-e2e2-e2e2-e2e2e2e2e2e2")){
                                otaUpdateFlag=characteristic
                                Log.d("Read1", characteristic.uuid.toString())
                            }
                        }
                    }else if(service.uuid.toString().equals("0000feb5-0000-1000-8000-00805f9b34fb")){
                        for(characteristic in service.characteristics){
                            if(characteristic.uuid.toString().equals("d4d4d4d4-d4d4-d4d4-d4d4-d4d4d4d4d4d4")||characteristic.uuid.toString().equals(
                                            "d5d5d5d5-d5d5-d5d5-d5d5-d5d5d5d5d5d5"
                                    )||characteristic.uuid.toString().equals("d6d6d6d6-d6d6-d6d6-d6d6-d6d6d6d6d6d6")){
                                readCharacteristics.add(characteristic)
                            }
                        }
                    }
                }
                gatt.readCharacteristic(readCharacteristics.get(count))
                count++
            } else {

            }
            Log.w("TAG", "onServicesDiscovered received: " + status)
        }

        override fun onCharacteristicRead(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(
                        "TAG", byteArrayToString(characteristic.value).substring(
                        0, byteArrayToString(
                        characteristic.value
                ).length - 2
                )
                )
                val value=byteArrayToString(characteristic.value).substring(
                        0, byteArrayToString(
                        characteristic.value
                ).length - 2)
                if(characteristic.uuid.toString().equals("d4d4d4d4-d4d4-d4d4-d4d4-d4d4d4d4d4d4")){
                    software_version.text=value.substring(0,2)+"."+value.substring(2,4)+"."+value.substring(4,6)
                }else if(characteristic.uuid.toString().equals("d5d5d5d5-d5d5-d5d5-d5d5-d5d5d5d5d5d5")){
                    software_date.text=value.substring(0,2)+"."+value.substring(2,4)+"."+value.substring(4,6)
                }else if(characteristic.uuid.toString().equals("d6d6d6d6-d6d6-d6d6-d6d6-d6d6d6d6d6d6")){
                    device_version.text=value.substring(0,2)+"."+value.substring(2,4)+"."+value.substring(4,6)
                }
                if(count<readCharacteristics.size) {
                    gatt.readCharacteristic(readCharacteristics.get(count))
                    count++
                }else{
                    LoadingUtils.hideDialog()
                }
            }
        }

        override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic
        ) {
            Log.w("TAG", "Characteristic Changed")
        }

        override fun onCharacteristicWrite(
                gatt: BluetoothGatt?,
                characteristic: BluetoothGattCharacteristic?,
                status: Int
        ) {
            super.onCharacteristicWrite(gatt, characteristic, status)
            Log.d("Status", status.toString())
            if(!otaDoneFlag)
            sendBlock()


        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_device_detail)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        LoadingUtils.showDialog(this, true)
        deviceId = intent?.extras?.getString("device").orEmpty()
        Log.d("Inpiut", input.toString())
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val mBluetoothDevice = bluetoothManager.adapter.getRemoteDevice(deviceId)
        if (Build.VERSION.SDK_INT < 23) {
            gatt = mBluetoothDevice.connectGatt(this, false, mGattCallback)
        } else {
            gatt = mBluetoothDevice.connectGatt(
                    this,
                    false,
                    mGattCallback,
                    BluetoothDevice.TRANSPORT_LE
            )
        }
        button.setOnClickListener(View.OnClickListener {
//            input= "4507b6f3169ae7937d3d4b8a3170498f".decodeHex().toCharArray()

            Log.d("Input", input.toString())
            val intent = Intent().setType("*/*").setAction(Intent.ACTION_GET_CONTENT)
            startActivityForResult(Intent.createChooser(intent, "Select a file"), 111)
        })
    }
    fun String.decodeHex():ByteArray=chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    fun byteArrayToString(byteArray: ByteArray):String{
        var string:String=""
        for (b in byteArray) {
            val st = String.format("%02X", b)
            string+=st
        }
        return string
    }
    override fun onBackPressed() {
        super.onBackPressed()
        gatt.disconnect()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if(requestCode==111&&resultCode== RESULT_OK){
            val selectedFile=data?.data
            val path = selectedFile?.let { if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.KITKAT) {
                FileChooser.getPath(this, it)
            } else {
                TODO("VERSION.SDK_INT < KITKAT")
            }
            }
            Log.d("Data", path)
            file= path?.let { File.getByFileName(it) }!!
            file?.setFileBlockSize(3, 240)
//            rc5Setup(input)
            PreferenceController.instance?.getKeyString(this, "Key")?.let { rc5Setup(it.decodeHex()) }
            progress_layout.visibility=View.VISIBLE
            button.visibility=View.GONE
            sendBlock()
        }
    }


    fun sendBlock(): Float{

        val progress = (chunkCounter + 1).toFloat() / file.getBlock(blockCounter)!!.size.toFloat() * 100


        runOnUiThread {
            if(!lastBlockSent) {
                percent.setText(progress.toInt().toString())
                progress_bar.progress = progress.toInt()
                Log.d("Percentage%", "$progress%")
            }else{
                Log.d("Percentage100", "100")

                percent.setText("100")
                progress_bar.progress =100
            }
        }
        if (!lastBlockSent) {
            Log.d("Percentage", "$progress%")
            val block: Array<ByteArray?>? = file.getBlock(blockCounter)
            val i: Int = ++chunkCounter
            if (chunkCounter == 0) Log.d(
                    "TAG",
                    "Current block: " + (blockCounter + 1) + " of " + file.numberOfBlocks
            )
            var lastChunk = false
            if (block != null) {
                if (chunkCounter == block.size - 1) {
                    chunkCounter = -1
                    lastChunk = true
                }
            }
            val chunk = block?.get(i)
            val chunkNumber: Int = blockCounter * file.chunksPerBlockCount + i + 1
             val systemLogMessage = "Sending block " + (blockCounter + 1) + ", chunk " + (i + 1) + " of " + block!!.size + ", size " + chunk!!.size
            Log.d("TAG", systemLogMessage)
            val characteristic: BluetoothGattCharacteristic =selectedCharacteristic
            var buf: ByteArray

//            val rc5 = RC5()
//            rc5.rc5_KeyIninitialize(input)

            var finalBytes:ByteArray= byteArrayOf(chunk.size.toByte())
            for(i in 0..chunk.size step 4){
                if(i!=0){
                    one=(chunk[i - 1].toInt() shl 8 xor chunk[i - 2].toInt()).toChar()
                    two=(chunk[i - 3].toInt() shl 8 xor chunk[i - 4].toInt()).toChar()
                    three=(chunk[i - 1].toInt() shl 8 xor chunk[i - 2].toInt())
                    four=(((chunk[i - 3].toInt() and 0xff) shl 8) xor chunk[i - 4].toInt())
//                    buf= charArrayOf(
//                        (chunk[i - 1].toInt() shl 8 xor chunk[i - 2].toInt()).toChar(),
//                        (chunk[i - 3].toInt() shl 8 xor chunk[i - 4].toInt()).toChar()
//                    )
//                    obuf = rc5.rc5_Encrypt(buf)
//                    dbuf = rc5.rc5_Decrypt(obuf)
                    buf= byteArrayOf(chunk[i - 4], chunk[i - 3], chunk[i - 2], chunk[i - 1])
                    obuf= rc5Encrypt(buf)
                    dbuf= rc5Decrypt(obuf)
                    val obufval=obuf[0].toInt() and 0xff00
                    finalBytes+= byteArrayOf(
                            obuf[0],
                            obuf[1],
                            obuf[2],
                            obuf[3]
                    )
                }
            }





            characteristic.value =finalBytes
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            val r: Boolean = gatt.writeCharacteristic(characteristic)
            Log.d("TAG", "writeCharacteristic: $r")
            if (lastChunk) {

                // SUOTA
                if (file.numberOfBlocks === 1) {
                    lastBlock = true
                }
                if (!lastBlock) {
                    blockCounter++
                } else {
                    lastBlockSent = true
                }
                if (blockCounter + 1 == file.numberOfBlocks) {
                    lastBlock = true
                }

/*                // SPOTA
                if (type == SpotaManager.TYPE) {
                    lastBlockSent = true
                }*/
            }
        }else{
            if(!otaDoneFlag) {
                val characteristic: BluetoothGattCharacteristic = otaUpdateFlag
                characteristic.value = byteArrayOf(1)
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                val r: Boolean = gatt.writeCharacteristic(characteristic)
                otaDoneFlag = true
            }
        }
        return progress
    }

    fun stringToCharArray(inputString: String?): CharArray? {
        var charArray:CharArray= CharArray(16)
        var count:Int=0
        if (inputString != null) {
            for(i in 2..inputString.length step 2){
            charArray.set(
                    count,
                    Integer.decode("0x" + inputString[i - 2] + inputString[i - 1]).toChar()
            )
            }
        }
        return charArray
    }
    fun charsToBytes(chars: CharArray?): ByteArray? {
        val byteBuffer: ByteBuffer = StandardCharsets.UTF_8.encode(CharBuffer.wrap(chars))
        return Arrays.copyOf(byteBuffer.array(), byteBuffer.limit())
    }

    fun bytesToChars(bytes: ByteArray?): CharArray? {
        val charBuffer: CharBuffer = StandardCharsets.UTF_8.decode(ByteBuffer.wrap(bytes))
        return Arrays.copyOf(charBuffer.array(), charBuffer.limit())
    }
}

private fun ByteArray.toCharArray(): CharArray {
    var chars:CharArray= CharArray(16)
    for(i in 0..15){
        if(this[i].toChar()>0xff00.toChar())
        chars.set(i, this[i].toChar() - 0xff00)
        else
            chars.set(i, this[i].toChar())
    }
    return chars
}


external fun rc5Setup(entry: ByteArray)
external fun rc5Decrypt(entry: ByteArray): ByteArray
external fun rc5Encrypt(entry: ByteArray): ByteArray
