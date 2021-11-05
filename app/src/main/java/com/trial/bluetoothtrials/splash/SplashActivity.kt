package com.trial.bluetoothtrials

import android.Manifest
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.LocationManager
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.provider.Settings
import android.util.Log
import android.view.Gravity
import android.view.ViewGroup
import android.view.Window
import androidx.appcompat.widget.AppCompatButton
import androidx.core.content.ContextCompat
import com.trial.bluetoothtrials.Utility.PermissionManager
import java.util.*
import kotlin.concurrent.schedule

class SplashActivity : AppCompatActivity() {
     var dialog: Dialog? =null
    private val permissions = arrayOf(
            Manifest.permission.ACCESS_COARSE_LOCATION
    )
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_splash)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)


      checkLocationPermission()

    }
    private fun intentCall(){
        Timer("SettingUp", false).schedule(2000) {
            val intent= Intent(this@SplashActivity, DashBoardActivity  ::class.java)
            startActivity(intent)
            finish()
        }
    }

    private fun checkLocationPermission() {
        if (PermissionManager.checkCoarseLocationPermission(this)) {
            if(locationEnabled()) {
                intentCall()
            }
        }else{
            checkPermissions()
        }
    }


    private fun checkPermissions(){
        if(PermissionManager.checkCoarseLocationPermission(this)){
            if(locationEnabled()){
                intentCall()
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
            try {
                if (grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                    if (locationEnabled()) {
                        Log.d("True","PERM")
                        intentCall()
                    }
                } else {
                    Log.d("Else","PERM")
                    PermissionManager.showAlertForPermission(
                            this@SplashActivity,
                            R.string.provide_perm_message
                    )

                }
            }catch (e: java.lang.Exception){
                Log.d("Exception", e.toString())
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
             dialog = Dialog(this@SplashActivity)
            dialog!!.requestWindowFeature(Window.FEATURE_NO_TITLE)
            dialog!!.setCancelable(false)
            dialog!!.setContentView(R.layout.dialog_location)
            val window = dialog!!.window
            window.setLayout(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
            )
            window.setGravity(Gravity.CENTER)
            val yesBtn = dialog!!.findViewById(R.id.settings) as AppCompatButton
            val noBtn = dialog!!.findViewById(R.id.cancel) as AppCompatButton
            yesBtn.setOnClickListener {
                dialog!!.dismiss()
                startActivity(
                        Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS)
                )
            }
            noBtn.setOnClickListener { dialog!!.dismiss() }
            dialog!!.show()
            return false
        }else{
            return true
        }
    }

    override fun onResume() {
        super.onResume()
        dialog?.dismiss()
        checkLocationPermission()
    }
}