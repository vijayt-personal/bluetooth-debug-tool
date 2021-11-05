package com.trial.bluetoothtrials.Utility

import android.content.Context
import android.content.SharedPreferences

var SHARED_PREF = "PREF"

class PreferenceController private constructor() {
    fun clear() {
        ourInstance = null
    }

    operator fun get(context: Context): SharedPreferences {
        return context.getSharedPreferences(SHARED_PREF, Context.MODE_PRIVATE)
    }

    fun addKeyString(context: Context, key: String?, `val`: String?) {
        val settings = instance!![context]
        val editor = settings.edit()
        editor.putString(key, `val`)
        editor.apply()
    }

    fun addKeyInt(context: Context, key: String?, `val`: Int) {
        val settings = instance!![context]
        val editor = settings.edit()
        editor.putInt(key, `val`)
        editor.apply()
    }

    fun addKeyILong(context: Context, key: String?, `val`: Long) {
        val settings = instance!![context]
        val editor = settings.edit()
        editor.putLong(key, `val`)
        editor.apply()
    }

    fun addKeyBoolean(context: Context, key: String?, `val`: Boolean) {
        val settings = instance!![context]
        val editor = settings.edit()
        editor.putBoolean(key, `val`)
        editor.apply()
    }

    fun getKeyString(context: Context, key: String?): String {
        val prefs = instance!![context]
        return prefs.getString(key, "")
    }

    fun getKeyInt(context: Context, key: String?): Int {
        val prefs = instance!![context]
        return prefs.getInt(key, 0)
    }

    fun getKeyLong(context: Context, key: String?): Long {
        val prefs = instance!![context]
        return prefs.getLong(key, 0)
    }

    fun getKeyBoolean(context: Context, key: String?): Boolean {
        val prefs = instance!![context]
        return prefs.getBoolean(key, false)
    }

    fun removeKey(context: Context, key: String?) {
        val settings = instance!![context]
        val editor = settings.edit()
        editor.remove(key)
        editor.apply()
    }



    companion object {
        private var ourInstance: PreferenceController? = PreferenceController()
        val instance: PreferenceController?
            get() {
                if (ourInstance == null) ourInstance = PreferenceController()
                return ourInstance
            }

        fun clearPreference(context: Context) {
            val settings = instance!![context]
            val editor = settings.edit()
            editor.clear()
            editor.commit()
        }
    }
}
