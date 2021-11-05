package com.trial.bluetoothtrials

import android.bluetooth.*
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.hbisoft.pickit.PickiT
import com.hbisoft.pickit.PickiTCallbacks
import com.trial.bluetoothtrials.Utility.File
import com.trial.bluetoothtrials.Utility.FileChooser
import com.trial.bluetoothtrials.Utility.LoadingUtils
import com.trial.bluetoothtrials.Utility.PreferenceController
import kotlinx.android.synthetic.main.activity_device_detail.*
import kotlinx.android.synthetic.main.progress_layout.*


class DeviceDetailActivity : AppCompatActivity(),PickiTCallbacks {
    private var negotiatedBlkSize: Int=0
    private var isFileTransferStarted: Boolean=false
    private var startTime: Long=0
    private var endTime: Long=0
    private lateinit var descriptor:BluetoothGattDescriptor
    private var isDefaultWrite: Boolean = false
    private var count:Int =0
    private var otaDoneFlag: Boolean=false
    lateinit var otaUpdateFlag: BluetoothGattCharacteristic
    lateinit var selectedCharacteristic: BluetoothGattCharacteristic
    lateinit var file: File
    var gatt: BluetoothGatt?=null
    private var deviceId: String=""
    var chunkCounter = -1
    var blockCounter:Int = 0
    var counter:Int=0
    var lastBlockSent = false
    var lastBlock = false
    var readCharacteristics=arrayListOf<BluetoothGattCharacteristic>()
    init {
        System.loadLibrary("rc5-lib")
    }

    var pickiT: PickiT? = null
    var obuf = ByteArray(4)

    private val mGattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val intentAction: String
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d("TAG", "Connected to GATT server.")
                // Attempts to discover services after successful connection.
                if (Build.VERSION.SDK_INT >= 21) {
                    gatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
//                    gatt.setPreferredPhy(PHY_LE_2M_MASK,PHY_LE_2M_MASK,PHY_OPTION_NO_PREFERRED)
                    gatt.requestMtu(251)
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i("TAG", "Disconnected from GATT server.")

                LoadingUtils.hideDialog()
                runOnUiThread {
                    if(otaDoneFlag) {
                        Toast.makeText(applicationContext, "Update Completed", Toast.LENGTH_SHORT).show()
                    }else {
                        Toast.makeText(
                            applicationContext,
                            "The device connection was lost",
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                    }

                finish()

            }
        }


        override fun onMtuChanged(gatt: BluetoothGatt?, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d("MTU",mtu.toString())
                negotiatedBlkSize=mtu-3-2
                negotiatedBlkSize = negotiatedBlkSize - negotiatedBlkSize%4
                Log.e("TAG", "MTU request Success, status=${status.toString()}")
            } else {
                Log.e("TAG", "MTU request failure, status=${status.toString()}")
            }
            Log.d("TAG", "Attempting to start service discovery:" + gatt!!.discoverServices())

        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt?,
            descriptor: BluetoothGattDescriptor?,
            status: Int
        ) {
            Log.d("TAG", "WRITE Done" + (descriptor?.uuid ?: ""))
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val services: List<BluetoothGattService> = gatt.getServices()
                for (service in services) {
                    if(service.uuid.toString().equals("e0e0e0e0-e0e0-e0e0-e0e0-e0e0e0e0e0e0")){
                        for(characteristic in service.characteristics){
                            if(characteristic.uuid.toString().equals("e5e5e5e5-e5e5-e5e5-e5e5-e5e5e5e5e5e5")){
                                isDefaultWrite=true
                                selectedCharacteristic=characteristic
                                Log.d("Read", characteristic.uuid.toString())
                            }
                            if(characteristic.uuid.toString().equals("e6e6e6e6-e6e6-e6e6-e6e6-e6e6e6e6e6e6")){
                                isDefaultWrite=false
                                selectedCharacteristic=characteristic
                                gatt.setCharacteristicNotification(selectedCharacteristic, true)
                                descriptor=selectedCharacteristic.getDescriptor(
                                    selectedCharacteristic.descriptors[0].uuid
                                )
                                descriptor.value=BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                Log.d("Selected UUID", descriptor.uuid.toString())

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
                if(readCharacteristics.size>0) {
                    gatt.readCharacteristic(readCharacteristics.get(count))
                    count++
                }
            } else {
                gatt.disconnect()
            }
            Log.w("TAG", "onServicesDiscovered received: " + status)
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            Log.d("Read", "TRUE")
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
                    ).length - 2
                )
                //Gets the version numbers from the device and set the UI
                runOnUiThread {
                    if (characteristic.uuid.toString() == "d4d4d4d4-d4d4-d4d4-d4d4-d4d4d4d4d4d4"
                    ) {
                        software_version.text = value.substring(0, 2) + "." + value.substring(
                            2,
                            4
                        ) + "." + value.substring(4, 6)
                    } else if (characteristic.uuid.toString() == "d5d5d5d5-d5d5-d5d5-d5d5-d5d5d5d5d5d5"
                    ) {
                        software_date.text = value.substring(0, 2) + "." + value.substring(
                            2,
                            4
                        ) + "." + value.substring(4, 6)
                    } else if (characteristic.uuid.toString() == "d6d6d6d6-d6d6-d6d6-d6d6-d6d6d6d6d6d6"
                    ) {
                        device_version.text = value.substring(0, 2) + "." + value.substring(
                            2,
                            4
                        ) + "." + value.substring(4, 6)
                    }
                }
                //Reads the version characteristics to get the versions
                    if(count<readCharacteristics.size) {
                        gatt.readCharacteristic(readCharacteristics.get(count))
                        count++
                    }else{
                        LoadingUtils.hideDialog()
                        //While ending the read process we also check if this process follows the write without response flow
                        if(!isDefaultWrite)
                        gatt.writeDescriptor(descriptor)
                    }
                }

        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if( counter.toByte().equals(characteristic.value[0])&&characteristic.value[1].equals(0.toByte())) {
                Log.w("TAG", "Characteristic Changed" + characteristic.value)
                if (isFileTransferStarted)
                    if (!isDefaultWrite)
                        if (!otaDoneFlag)
                            sendBlock()
            }else{
                gatt.disconnect()
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt?,
            characteristic: BluetoothGattCharacteristic?,
            status: Int
        ) {
            super.onCharacteristicWrite(gatt, characteristic, status)

            if(isDefaultWrite)
            if(!otaDoneFlag) {
                Log.d("TAG", "WRITE")
                sendBlock()
            }


        }
    }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_device_detail)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        LoadingUtils.showDialog(this, true)
        deviceId = intent?.extras?.getString("device").orEmpty()
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val mBluetoothDevice = bluetoothManager.adapter.getRemoteDevice(deviceId)
        pickiT = PickiT(this, this, this)
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
       finish()

    }

    override fun onDestroy() {
        super.onDestroy()
        gatt?.disconnect()
        gatt?.close()
        gatt=null
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if(requestCode==111&&resultCode== RESULT_OK){
            val selectedFile=data?.data
             pickiT?.getPath(data?.getData(), Build.VERSION.SDK_INT)
//            val path = selectedFile?.let { if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.KITKAT) {
//                FileChooser.getPath(this, it)
//            } else {
//                TODO("VERSION.SDK_INT < KITKAT")
//            }
//            }

        }
    }


    fun sendBlock(): Float{
        Log.d("Send block", "called")
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
             val systemLogMessage = "Sending block " + (blockCounter + 1) + ", chunk " + (i + 1) + " of " + block!!.size + ", size " + chunk!!.size
            Log.d("TAG", systemLogMessage)
            val characteristic: BluetoothGattCharacteristic =selectedCharacteristic
            Log.d("SELECTED CHAR",selectedCharacteristic.uuid.toString())
            var buf: ByteArray
            if(counter==255){
                counter=1
            }else{
                counter++
            }
            var finalBytes:ByteArray
            if(isDefaultWrite){
                finalBytes=byteArrayOf(chunk.size.toByte())
            }else{
                finalBytes= byteArrayOf(counter.toByte())
                finalBytes+=byteArrayOf(chunk.size.toByte())
            }

            for(i in 0..chunk.size step 4){
                if(i!=0){
                    buf= byteArrayOf(chunk[i - 4], chunk[i - 3], chunk[i - 2], chunk[i - 1])
                    obuf= rc5Encrypt(buf)
                    finalBytes+= byteArrayOf(
                        obuf[0],
                        obuf[1],
                        obuf[2],
                        obuf[3]
                    )
                }
            }

            characteristic.value =finalBytes
            if(isDefaultWrite)
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            else
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            val r: Boolean = gatt?.writeCharacteristic(characteristic) ?: false
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

            }
        }else{
            if(!otaDoneFlag) {
                val characteristic: BluetoothGattCharacteristic = otaUpdateFlag
                characteristic.value = byteArrayOf(1)
                if(isDefaultWrite)
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                else
                    characteristic.writeType=BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                val r: Boolean = gatt?.writeCharacteristic(characteristic) ?: false
                otaDoneFlag = true
                Log.d("OTA UPDATE", "DONE " + (System.currentTimeMillis() - startTime))
            }
        }
        return progress
    }

    override fun PickiTonUriReturned() {
//        TODO("Not yet implemented")
    }

    override fun PickiTonStartListener() {
//        TODO("Not yet implemented")
    }

    override fun PickiTonProgressUpdate(progress: Int) {
//        TODO("Not yet implemented")
    }

    override fun PickiTonCompleteListener(
        path: String?,
        wasDriveFile: Boolean,
        wasUnknownProvider: Boolean,
        wasSuccessful: Boolean,
        Reason: String?
    ) {
        Log.d("Data", path)
        isFileTransferStarted=true
        file= path?.let { File.getByFileName(it) }!!

        file?.setFileBlockSize(16, negotiatedBlkSize)
        Log.d("File size",file.fileBlockSize.toString()+" "+negotiatedBlkSize)
        Log.d("KEY", PreferenceController.instance?.getKeyString(this, "Key"))
        PreferenceController.instance?.getKeyString(this, "Key")?.let { rc5Setup(it.decodeHex()) }
        progress_layout.visibility=View.VISIBLE
        button.visibility=View.GONE
        startTime=System.currentTimeMillis()
        sendBlock()
    }

}



external fun rc5Setup(entry: ByteArray)
external fun rc5Decrypt(entry: ByteArray): ByteArray
external fun rc5Encrypt(entry: ByteArray): ByteArray
