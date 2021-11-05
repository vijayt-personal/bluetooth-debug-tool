package com.trial.bluetoothtrials


import android.bluetooth.BluetoothAdapter
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.os.Bundle
import android.os.ParcelUuid
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import com.trial.bluetoothtrials.Utility.PreferenceController
import kotlinx.android.synthetic.main.activity_advertisement.*
import java.util.*


class AdvertisementActivity : AppCompatActivity() {

    init {
        System.loadLibrary("aes-lib")
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_advertisement)
        val key= PreferenceController.instance?.getKeyString(this, "Key")!!.decodeHex()
        val a= aesEncrypt(key, key)
        val b= aesDecrypt(a, key)
        if (key != null) {
            textView.text=a.toString()
        }
        val advertiser = BluetoothAdapter.getDefaultAdapter().bluetoothLeAdvertiser
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(false)
            .build()

        val pUuid = ParcelUuid(UUID.fromString(getString(R.string.ble_uuid)))

        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .addManufacturerData(0x197.toInt(), byteArrayOf(0x01,0x02))
            .build()

        val advertisingCallback: AdvertiseCallback = object : AdvertiseCallback() {
            override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
                super.onStartSuccess(settingsInEffect)
            }

            override fun onStartFailure(errorCode: Int) {
                Log.e("BLE", "Advertising onStartFailure: $errorCode")
                super.onStartFailure(errorCode)
            }
        }
        advertiser.startAdvertising(settings, data, advertisingCallback)
    }


}
fun String.decodeHex():ByteArray=chunked(2).map { it.toInt(16).toByte() }.toByteArray()


external fun aesDecrypt(entry: ByteArray, key: ByteArray): ByteArray
external fun aesEncrypt(entry: ByteArray, key: ByteArray): ByteArray