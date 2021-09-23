package com.trial.bluetoothtrials

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.util.Log
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.trial.bluetoothtrials.Utility.PermissionManager
import kotlinx.android.synthetic.main.activity_logger_scan.*
import java.text.SimpleDateFormat
import java.util.*
import kotlin.collections.ArrayList


class LoggerScanActivity : AppCompatActivity(),ItemClick {
    private lateinit var callback: ScanCallback
    private var settings: ScanSettings? = null
    private var scanner: BluetoothLeScanner? = null
    private var scanning: Boolean = false
    var scannedDevice=arrayListOf<Device>()
    var scannedHex=arrayListOf<String>()
    var scanTimer: Runnable? = null
    var handler: Handler? = Handler()
    lateinit var mBluetoothManager:BluetoothManager
    lateinit var mBluetoothAdapter: BluetoothAdapter
    private lateinit var linearLayoutManager: LinearLayoutManager
    private lateinit var adapter:LoggerScanAdapter
    private val permissions = arrayOf(
        Manifest.permission.ACCESS_COARSE_LOCATION
    )
    private var doubleBackToExitPressedOnce = false
    var addressVal:String="N/A"
    var nameVal:String="N/A"
    var rawVal:String="N/A"
    var loggerList: MutableMap<String, ArrayList<LogRecord>> = mutableMapOf()

    private fun isMacAlreadyScanned(tagId: String): Boolean {
        for( scandevice in scannedDevice){
            if(scandevice.scanHex.subSequence(18, 26).equals(tagId)){
                return true
            }
        }
        return false
    }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_logger_scan)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        linearLayoutManager = LinearLayoutManager(this)
        device_list.layoutManager = linearLayoutManager
        adapter = LoggerScanAdapter(scannedDevice, this)
        device_list.adapter = adapter
        // Initialize Bluetooth adapter
//        mBluetoothManager=applicationContext.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        mBluetoothAdapter=  BluetoothAdapter.getDefaultAdapter()

        floatingActionButton.setOnClickListener(View.OnClickListener {
            if (!scanning) {
//                if(mBluetoothAdapter.isEnabled) {
                    progress_horizontal.visibility = View.VISIBLE
                    checkPermissionsandStartScan()
//                }else{
//                    Toast.makeText(applicationContext,"Please enable bluetooth to start scanning for devices",Toast.LENGTH_SHORT).show()
//                }
            } else {
                progress_horizontal.visibility = View.GONE
                stopDeviceScan()
            }
        })


        adapter.registerAdapterDataObserver(object : RecyclerView.AdapterDataObserver() {
            override fun onChanged() {
                if (adapter.itemCount == 0) {
                    device_not_found.visibility = View.VISIBLE
//                    device_list.visibility=View.GONE
                } else {
                    device_not_found.visibility = View.GONE
//                    adapter.filter.filter("970152")
//                    device_list.visibility=View.VISIBLE
                }
            }
        })
    }


    //checks for the permissions and shows permission dialog if no permission found else starts scan
    private fun checkPermissionsandStartScan(){
        if(PermissionManager.checkCoarseLocationPermission(this)){
            startDeviceScan()
        }else{
            PermissionManager.requestPermissionsForAttachmentActivity(this, permissions)
        }
    }

    override fun onPause() {
        super.onPause()
        stopDeviceScan()
    }

    //starts ble scan
    private fun startDeviceScan(){
//        scannedHex.clear()
//        scannedDevice.clear()
        adapter.notifyDataSetChanged()
        if (scanner == null) {
            scanner = mBluetoothAdapter.bluetoothLeScanner
            settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).setReportDelay(
                0
            ).build()
            callback = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    Log.d("ScanRecords", bytesToHex(result.scanRecord.bytes))

//                Log.d("ScanRecords", bytesToHex(result.scanRecord.bytes))
//                if (!scannedHex.contains(bytesToHex(scanRecord))) {
                    val device=result.device
                    val scanRecord=result.scanRecord.bytes
                    val tagid=bytesToHex(scanRecord).subSequence(18, 26).toString()
                    val rssi=result.rssi
                    var isConnectable:Boolean=false
                    if(android.os.Build.VERSION.SDK_INT >26)
                        isConnectable=result.isConnectable
                    else
                        isConnectable=true
                    val hexstring=bytesToHex(scanRecord)
                    if(hexstring.contains("970152")&&(hexstring.subSequence(16, 18).equals("04")||hexstring.subSequence(
                            16,
                            18
                        ).equals("05")||hexstring.subSequence(16, 18).equals("06")))
                    if (!isMacAlreadyScanned(tagid)) {
                        var results:ArrayList<LogRecord> = arrayListOf()
                        val date = Date(System.currentTimeMillis())

                        val formatter = SimpleDateFormat("HH:mm:ss.SSS")
//                        formatter.setTimeZone(TimeZone.getTimeZone("UTC"))

                        val formatted: String = formatter.format(date)
                        val record:LogRecord= LogRecord(bytesToHex(scanRecord),formatted,
                            rssi.toString()
                        )
                        if(loggerList[tagid]!=null)
                        results= loggerList[tagid]!!
                        if (results != null) {
                            results.add(record)
                        }
                        loggerList[tagid]=results
//                        loggerList.plus(device.address to results)
                        var name: String? = ""
                        if (device.name != null)
                            name = device.name
                        else
                            name = "NA"
                        val scanHexRecord = Device(
                            device.address,
                            bytesToHex(scanRecord),
                            name,
                            rssi,
                            isConnectable
                        )
                        scannedHex.add(bytesToHex(scanRecord))
                        scannedDevice.add(scanHexRecord)
//                        Log.d("Added", "new record"+rssi)
                        adapter.notifyDataSetChanged()
                    } else {
                        var results:ArrayList<LogRecord> = arrayListOf()
                        val date = Date(System.currentTimeMillis())

                        val formatter = SimpleDateFormat("HH:mm:ss.SSS")
//                        formatter.setTimeZone(TimeZone.getTimeZone("UTC"))

                        val formatted: String = formatter.format(date)
                        val record:LogRecord= LogRecord(bytesToHex(scanRecord),formatted,
                            rssi.toString()
                        )
                        if(loggerList[tagid]!=null)
                            results= loggerList[tagid]!!
                        if (results != null) {
                            results.add(record)
                        }
                        loggerList[tagid]=results
                        var position: Int = -1
                        for (i in 0..scannedDevice.size) {
                            Log.d("SCannedDEV", scannedDevice.size.toString())
                            if(i<scannedDevice.size)
                                if (scannedDevice.get(i).scanHex.subSequence(18, 26).toString().equals(
                                        tagid
                                    )) {
                                    position = i
                                    Log.d("pos", position.toString())
                                    break
                                }
//                            }else{
//                                break
//                            }
                        }

                        var name: String? = ""
                        if (device.name != null)
                            name = device.name
                        else
                            name = "NA"
                        val scanHexRecord = Device(
                            device.address,
                            bytesToHex(scanRecord),
                            name,
                            rssi,
                            isConnectable
                        )
                        if(position!=-1) {
                            Log.d("Update", bytesToHex(scanRecord))
                            scannedHex.set(position, bytesToHex(scanRecord))
                            scannedDevice.set(position, scanHexRecord)
//                        Log.d("Added", "new record"+rssi)
                            adapter.notifyItemChanged(position)
                        }

                    }

//                }


                }


            }
        }
        scanner?.startScan(null, settings, callback)
        scanning=true
//        scanTimer = Runnable { stopDeviceScan() }
//        scanTimer?.let { handler!!.postDelayed(it, 30000) }
//        if(adapter.itemCount==0)
//        {
//            device_not_found.visibility=View.VISIBLE
//            device_list.visibility=View.GONE
//        }else{
//            device_not_found.visibility=View.GONE
//            device_list.visibility=View.VISIBLE
//        }
    }

    //Stops the ble scan
    private fun stopDeviceScan() {
//        handler!!.removeCallbacks(scanTimer!!)
        if (scanner != null && mBluetoothAdapter!!.isEnabled) scanner!!.stopScan(callback)
        scanning=false
        progress_horizontal.visibility=View.GONE
        if(adapter.itemCount==0)
        {
            device_not_found.visibility=View.VISIBLE
//            device_list.visibility=View.GONE
        }else{
            device_not_found.visibility=View.GONE
//            device_list.visibility=View.VISIBLE
        }
        Log.d("Map", loggerList.size.toString())
    }

    //Coverts byte array to hex string
    fun bytesToHex(bytes: ByteArray): String {
        var hexString:String=""
        for (b in bytes) {
            val hexElement = String.format("%02X", b)
            hexString+=hexElement
        }
        return hexString
    }

    //We get the permission request results over here
    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if(requestCode== PermissionManager.REQUEST_FOR_PERMISSION){
            if(grantResults[0]==PackageManager.PERMISSION_GRANTED){
                startDeviceScan()
            }else{
                Toast.makeText(this, "Location permission denied", Toast.LENGTH_LONG)
                    .show()
                PermissionManager.showAlertForPermission(
                    this@LoggerScanActivity,
                    R.string.provide_perm_message
                )

            }
        }
    }

    override fun onResume() {
        super.onResume()

        if(!mBluetoothAdapter.isEnabled) {
            mBluetoothAdapter.enable()
            Toast.makeText(applicationContext,"Enabling Bluetooth....",Toast.LENGTH_SHORT).show()
            Handler().postDelayed({
                //doSomethingHere()
                progress_horizontal.visibility = View.VISIBLE
                checkPermissionsandStartScan()
            }, 3000)
        }else {
            progress_horizontal.visibility = View.VISIBLE
            checkPermissionsandStartScan()
        }

    }

    override fun onItemClick(device: Device) {
        Log.d("On", "Item Click")
        val intent=Intent(this, LoggerActivity::class.java)
//        intent.putExtra("device", device.address)
        intent.putParcelableArrayListExtra(
            "logList",
            loggerList[device.scanHex.subSequence(18, 26)]
        )
        startActivity(intent)

    }

    override fun onBackPressed() {
        finish()

//        if (doubleBackToExitPressedOnce) {
//            System.exit(0)
//            return
//        }
//
//        this.doubleBackToExitPressedOnce = true
//        Toast.makeText(this, "Please click BACK again to exit", Toast.LENGTH_SHORT).show()
//
//        Handler().postDelayed(Runnable { doubleBackToExitPressedOnce = false }, 3000)
    }
}