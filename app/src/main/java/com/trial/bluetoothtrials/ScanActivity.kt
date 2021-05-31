package com.trial.bluetoothtrials

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.text.Editable
import android.text.TextWatcher
import android.util.Log
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.trial.bluetoothtrials.Utility.PermissionManager
import kotlinx.android.synthetic.main.activity_main.*
import kotlinx.android.synthetic.main.hidden_layout.*


class ScanActivity : AppCompatActivity(),ItemClick {
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
    private lateinit var adapter:CustomAdapter
    private val permissions = arrayOf(
            Manifest.permission.ACCESS_COARSE_LOCATION
    )
    var addressVal:String="N/A"
    var nameVal:String="N/A"
    var rawVal:String="N/A"


    private fun isMacAlreadyScanned(device: BluetoothDevice): Boolean {
    for( scandevice in scannedDevice){
        if(scandevice.address.equals(device.address)){
            return true
        }
    }
        return false
    }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        linearLayoutManager = LinearLayoutManager(this)
        device_list.layoutManager = linearLayoutManager
        adapter = CustomAdapter(scannedDevice, this)
        device_list.adapter = adapter
        // Initialize Bluetooth adapter
//        mBluetoothManager=applicationContext.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        mBluetoothAdapter=  BluetoothAdapter.getDefaultAdapter()

        floatingActionButton.setOnClickListener(View.OnClickListener {
            if (!scanning) {
                progress_horizontal.visibility = View.VISIBLE
                checkPermissionsandStartScan()
            } else {
                progress_horizontal.visibility = View.GONE
                stopDeviceScan()
            }
        })

        search_value.setOnClickListener(View.OnClickListener {
            if (hiddenview.getVisibility() === View.VISIBLE) {

                // The transition of the hiddenView is carried out
                //  by the TransitionManager class.
                // Here we use an object of the AutoTransition
                // Class to create a default transition.
                search_value.setCompoundDrawablesWithIntrinsicBounds(
                        0,
                        0,
                        R.drawable.ic_baseline_keyboard_arrow_down_24,
                        0
                );
                hiddenview.visibility = View.GONE
            } else {
                search_value.setCompoundDrawablesWithIntrinsicBounds(
                        0,
                        0,
                        R.drawable.ic_baseline_arrow_drop_up_24,
                        0
                );

                hiddenview.setVisibility(View.VISIBLE)

            }
        })

        filter_address.addTextChangedListener(textWatcherAddress)
        filter_name.addTextChangedListener(textWatcherName)
        filter_raw_data.addTextChangedListener(textWatcherRaw)
        search_value.addTextChangedListener(textWatcher)

        adapter.registerAdapterDataObserver(object : RecyclerView.AdapterDataObserver() {
            override fun onChanged() {
                if (adapter.itemCount == 0) {
                    device_not_found.visibility = View.VISIBLE
//                    device_list.visibility=View.GONE
                } else {
                    device_not_found.visibility = View.GONE
//                    device_list.visibility=View.VISIBLE
                }
            }
        })
    }

    private val textWatcher = object : TextWatcher {
        override fun afterTextChanged(s: Editable?) {
        }
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {
        }
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
            var str:CharSequence?= ""
            if(s.toString().equals("N/A,N/A,N/A"))
                search_value.text="Filter"
            if(!s.toString().equals("Filter"))
                str= s
            else
                 str = "" as CharSequence
            adapter.filter.filter(str)
        }
    }

    private val textWatcherAddress = object : TextWatcher {
        override fun afterTextChanged(s: Editable?) {
        }
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {
        }
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
          addressVal=s.toString()

            if(addressVal.isNotEmpty()||rawVal.isNotEmpty()||nameVal.isNotEmpty()){
                search_value.setText(nameVal + "," + addressVal + "," + rawVal)
                if(addressVal.isEmpty()||addressVal.equals("N/A")){
                    addressVal="N/A"
                    search_value.setText(nameVal + "," + "N/A" + "," + rawVal)
                }
            }else{
                if(addressVal.isEmpty()&&rawVal.isEmpty()&&nameVal.isEmpty()){
                    search_value.setText("Filter")
                }
            }
        }
    }
    private val textWatcherRaw = object : TextWatcher {
        override fun afterTextChanged(s: Editable?) {
        }
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {
        }
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
            rawVal=s.toString()


            if(addressVal.isNotEmpty()||rawVal.isNotEmpty()||nameVal.isNotEmpty()){
                search_value.setText(nameVal + "," + addressVal + "," + rawVal)
                if(rawVal.isEmpty()||rawVal.equals("N/A")){
                    rawVal="N/A"
                    search_value.setText(nameVal + "," + addressVal + "," + "N/A")
                }
            }else{
                if(addressVal.isEmpty()&&rawVal.isEmpty()&&nameVal.isEmpty()){
                    search_value.setText("Filter")
                }
            }
        }
    }

    private val textWatcherName = object : TextWatcher {
        override fun afterTextChanged(s: Editable?) {
        }
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {
        }
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
            nameVal=s.toString()
            if(addressVal.isNotEmpty()||rawVal.isNotEmpty()||nameVal.isNotEmpty()){
                search_value.setText(nameVal + "," + addressVal + "," + rawVal)
                if(nameVal.isEmpty()||nameVal.equals("N/A")){
                    nameVal="N/A"
                    search_value.setText("N/A" + "," + addressVal + "," + rawVal)
                }
            }else{
                if(addressVal.isEmpty()&&rawVal.isEmpty()&&nameVal.isEmpty()){
                    search_value.setText("Filter")
                }
            }
        }
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
        scannedHex.clear()
        scannedDevice.clear()
        adapter.notifyDataSetChanged()
        if (scanner == null) {
            scanner = mBluetoothAdapter.bluetoothLeScanner
            settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).setReportDelay(0).build()
            callback = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    Log.d("ScanRecords", bytesToHex(result.scanRecord.bytes))

//                Log.d("ScanRecords", bytesToHex(result.scanRecord.bytes))
//                if (!scannedHex.contains(bytesToHex(scanRecord))) {
                        val device=result.device
                        val scanRecord=result.scanRecord.bytes
                        val rssi=result.rssi
                        var isConnectable:Boolean=false
                        if(android.os.Build.VERSION.SDK_INT >26)
                        isConnectable=result.isConnectable
                        else
                            isConnectable=true
                        if (!isMacAlreadyScanned(device)) {
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
                            var position: Int = -1
                            for (i in 0..scannedDevice.size) {
//                                if(i<scannedDevice.size)
                                if (scannedDevice.get(i).address.equals(device.address)) {
                                    position = i
                                    break
                                }
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
                            scannedHex.set(position, bytesToHex(scanRecord))
                            scannedDevice.set(position, scanHexRecord)
//                        Log.d("Added", "new record"+rssi)
                            adapter.notifyItemChanged(position)

                        }

//                }


                }


            }
        }
        scanner?.startScan(null, settings, callback)
        scanning=true
        scanTimer = Runnable { stopDeviceScan() }
        scanTimer?.let { handler!!.postDelayed(it, 30000) }
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
        handler!!.removeCallbacks(scanTimer!!)
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
                        this@ScanActivity,
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
        val intent=Intent(this, DeviceDetailActivity::class.java)
        intent.putExtra("device", device.address)
        startActivity(intent)

    }


}