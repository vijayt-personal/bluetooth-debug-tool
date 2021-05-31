package com.trial.bluetoothtrials

import android.Manifest
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.AppCompatButton
import androidx.core.content.ContextCompat
import com.trial.bluetoothtrials.Utility.PermissionManager
import com.trial.bluetoothtrials.Utility.PreferenceController
import kotlinx.android.synthetic.main.activity_key.*

class KeyActivity : AppCompatActivity() {
    private val permissions = arrayOf(
        Manifest.permission.ACCESS_COARSE_LOCATION,
        Manifest.permission.READ_EXTERNAL_STORAGE,
        Manifest.permission.WRITE_EXTERNAL_STORAGE
    )
    var flow:String=""
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_key)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        val extras = intent.extras
        flow  = extras.getString("Flow")
        if(PreferenceController.instance?.getKeyString(this, "Key").isNullOrEmpty()){
            PreferenceController.instance?.addKeyString(
                this,
                "Key",
                "4507b6f3169ae7937d3d4b8a3170498f"
            )
        }
        checkPermissions()
        submit.setOnClickListener(View.OnClickListener {
            if (PermissionManager.checkCoarseLocationPermission(this) && PermissionManager.checkReadExtenalStoragePermission(
                    this
                )
            ) {
                if (!key.text.toString().isNullOrEmpty()) {
                    PreferenceController.instance?.addKeyString(this, "Key", key.text.toString())
                }
                if(locationEnabled()) {
                    if(flow.equals("OTA")) {
                        val intent = Intent(this, ScanActivity::class.java)
                        startActivity(intent)
                    }else if(flow.equals("Advertiser")){
                        val intent = Intent(this, AdvertisementActivity::class.java)
                        startActivity(intent)
                    }
                }
            }
        })

    }

    private fun checkPermissions(){
        if(PermissionManager.checkCoarseLocationPermission(this)){
            if(PermissionManager.checkReadExtenalStoragePermission(this)) {
            locationEnabled()
            }else{
                PermissionManager.requestPermissionsForAttachmentActivity(this, permissions)
            }
        }else{
            PermissionManager.requestPermissionsForAttachmentActivity(this, permissions)
        }
    }
    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if(requestCode== PermissionManager.REQUEST_FOR_PERMISSION){
            if(grantResults[0]== PackageManager.PERMISSION_GRANTED&&grantResults[1]== PackageManager.PERMISSION_GRANTED&&grantResults[2]== PackageManager.PERMISSION_GRANTED){
            locationEnabled()
            }else{
//                Toast.makeText(this, "Some permissions denied", Toast.LENGTH_LONG)
//                    .show()
                PermissionManager.showAlertForPermission(
                    this@KeyActivity,
                    R.string.provide_perm_message
                )

            }
        }
    }


    private fun locationEnabled():Boolean {
        val lm = getSystemService(Context.LOCATION_SERVICE) as LocationManager
        var gps_enabled = false
        var network_enabled = false
        try {
            gps_enabled = lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
        } catch (e: Exception) {
            e.printStackTrace()
        }
        try {
            network_enabled = lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
        } catch (e: Exception) {
            e.printStackTrace()
        }
        if (!gps_enabled && !network_enabled) {
            val dialog = Dialog(this@KeyActivity)
            dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)
            dialog.setCancelable(false)
            dialog.setContentView(R.layout.dialog_location)
            val window = dialog.window
            window.setLayout(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
            window.setGravity(Gravity.CENTER)
            val yesBtn = dialog.findViewById(R.id.settings) as AppCompatButton
            val noBtn = dialog.findViewById(R.id.cancel) as AppCompatButton
            yesBtn.setOnClickListener {
                dialog.dismiss()
                startActivity(
                    Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS)
                )
            }
            noBtn.setOnClickListener { dialog.dismiss() }
            dialog.show()
            return false
        }else{
            return true
        }
    }
}