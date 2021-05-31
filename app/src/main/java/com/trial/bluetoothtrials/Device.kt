package com.trial.bluetoothtrials

data class Device(val address:String, val scanHex:String,val name:String?,val rssi:Int,val connected:Boolean)
