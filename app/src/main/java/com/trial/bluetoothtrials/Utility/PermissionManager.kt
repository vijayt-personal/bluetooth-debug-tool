package com.trial.bluetoothtrials.Utility

import android.Manifest
import android.R
import android.annotation.TargetApi
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.trial.bluetoothtrials.BuildConfig

/**
 * Created by yadhukrishnan.e@oneteam.us
 */
object PermissionManager {
    const val REQUEST_FOR_PERMISSION = 1001


    /*
     * Add the required permission in this array and manifest
     */
    private val permissions = arrayOf(
        Manifest.permission.CAMERA,
        Manifest.permission.ACCESS_FINE_LOCATION,
        Manifest.permission.CALL_PHONE,
        Manifest.permission.WRITE_EXTERNAL_STORAGE
    )

    fun checkPermissions(context: Context): Boolean {
        for (permission in permissions) {
            val status = checkPermission(context, permission)
            if (!status) {
                return false
            }
        }
        return true
    }

    fun checkPermissionRationale(context: Activity?): Boolean {
        for (permission in permissions) {
            val status = isShouldShowRequestPermissionRationale(context, permission)
            if (!status) {
                return false
            }
        }
        return true
    }

    fun checkPermissionGranted(results: IntArray): Boolean {
        for (result in results) {
            if (result != PackageManager.PERMISSION_GRANTED) {
                return false
            }
        }
        return true
    }

    fun requestPermissions(context: Activity?) {
        ActivityCompat.requestPermissions(context!!, permissions, REQUEST_FOR_PERMISSION)
    }

    fun requestPermissionsForAttachmentActivity(context: Activity?, permissions: Array<String>) {
        ActivityCompat.requestPermissions(context!!, permissions!!, REQUEST_FOR_PERMISSION)
    }

    fun requestPermission(activity: AppCompatActivity?, permission: String) {
        ActivityCompat.requestPermissions(activity!!, arrayOf(permission), REQUEST_FOR_PERMISSION)
    }

    private fun checkPermission(context: Context, permission: String): Boolean {
        return ActivityCompat.checkSelfPermission(
            context,
            permission
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun checkCameraPermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.CAMERA
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun checkExternalStoragePermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.WRITE_EXTERNAL_STORAGE
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun checkCallPhonePermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.CALL_PHONE
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun checkReadExtenalStoragePermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.READ_EXTERNAL_STORAGE
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun checkLocationPermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun checkCoarseLocationPermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }

    @TargetApi(Build.VERSION_CODES.M)
    fun checkFingerprintPermission(context: Context?): Boolean {
        return ActivityCompat.checkSelfPermission(
            context!!,
            Manifest.permission.USE_FINGERPRINT
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun isShouldShowRequestPermissionRationale(context: Activity?, permission: String?): Boolean {
        return ActivityCompat.shouldShowRequestPermissionRationale(context!!, permission!!)
    }

    fun showAlertForPermission(context: Context, resource: Int) {

            val builder =
                AlertDialog.Builder(context, R.style.Theme_DeviceDefault_Light_Dialog_Alert)
            builder.setMessage(resource)
            builder.setNeutralButton(R.string.ok,
                DialogInterface.OnClickListener { dialog, which ->
                    dialog.dismiss()
                    val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                    val uri = Uri.fromParts("package", BuildConfig.APPLICATION_ID, null)
                    intent.data = uri
                    intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK
                    context.startActivity(intent)
                })
            val dialog = builder.create()
            dialog.show()
    }
}