package com.trial.bluetoothtrials

import android.os.Parcel
import android.os.Parcelable
import java.sql.Timestamp

data class LogRecord(val rawData:String, val timestamp: String,val rssi:String) : Parcelable {
    constructor(parcel: Parcel) : this(
        parcel.readString(),
        parcel.readString(),
        parcel.readString()
    ) {
    }

    override fun writeToParcel(parcel: Parcel, flags: Int) {
        parcel.writeString(rawData)
        parcel.writeString(timestamp)
        parcel.writeString(rssi)
    }

    override fun describeContents(): Int {
        return 0
    }

    companion object CREATOR : Parcelable.Creator<LogRecord> {
        override fun createFromParcel(parcel: Parcel): LogRecord {
            return LogRecord(parcel)
        }

        override fun newArray(size: Int): Array<LogRecord?> {
            return arrayOfNulls(size)
        }
    }
}
