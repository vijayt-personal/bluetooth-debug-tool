package com.trial.bluetoothtrials

import android.Manifest
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.LocationManager
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.os.Handler
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.widget.Toast
import androidx.appcompat.widget.AppCompatButton
import androidx.core.content.ContextCompat
import com.trial.bluetoothtrials.Utility.PermissionManager
import kotlinx.android.synthetic.main.activity_dash_board.*
import kotlinx.android.synthetic.main.activity_key.*

class DashBoardActivity : AppCompatActivity() {
    private var doubleBackToExitPressedOnce = false
    private val permissions = arrayOf(
        Manifest.permission.ACCESS_COARSE_LOCATION
    )
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_dash_board)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        ota_update.setOnClickListener(View.OnClickListener {
            if (PermissionManager.checkCoarseLocationPermission(this)) {
                if(locationEnabled()) {
                    val intent= Intent(this, KeyActivity  ::class.java)
                    intent.putExtra("Flow","OTA")
                    startActivity(intent)
                }
            }else{
                checkPermissions()
            }


        })
        logger.setOnClickListener(View.OnClickListener {
            if (PermissionManager.checkCoarseLocationPermission(this)) {
                if(locationEnabled()) {
                    val intent= Intent(this, LoggerScanActivity  ::class.java)
                    startActivity(intent)
                }
            }else{
                checkPermissions()
            }


        })


        advertiser_layout.setOnClickListener(View.OnClickListener {
            if (PermissionManager.checkCoarseLocationPermission(this)) {
                if(locationEnabled()) {
                    val intent= Intent(this, KeyActivity  ::class.java)
                    intent.putExtra("Flow","Advertiser")
                    startActivity(intent)
                }
            }else{
                checkPermissions()
            }


        })
    }

    override fun onBackPressed() {
        if (doubleBackToExitPressedOnce) {
            super.onBackPressed()
            return
        }

        this.doubleBackToExitPressedOnce = true
        Toast.makeText(this, "Please click BACK again to exit", Toast.LENGTH_SHORT).show()

        Handler().postDelayed(Runnable { doubleBackToExitPressedOnce = false }, 2000)
    }

    private fun checkPermissions(){
        if(PermissionManager.checkCoarseLocationPermission(this)){
                locationEnabled()
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
            if(grantResults[0]==PackageManager.PERMISSION_GRANTED){
                locationEnabled()
            }else{
                PermissionManager.showAlertForPermission(
                    this@DashBoardActivity,
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
            val dialog = Dialog(this@DashBoardActivity)
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