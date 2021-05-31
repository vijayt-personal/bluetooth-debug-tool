package com.trial.bluetoothtrials

import android.annotation.SuppressLint
import android.util.Base64
import com.trial.bluetoothtrials.Utility.PreferenceController
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

class AesEncryption private constructor() {
    private val privateKey:String=""
    @SuppressLint("GetInstance")
    fun encrypt(value: ByteArray,privateKey:String): String? {
        try {
//            val iv = IvParameterSpec(ivString.toByteArray(charset("UTF-8")))
            val skeySpec = SecretKeySpec(privateKey.toByteArray(charset("UTF-8")), "AES")
            val cipher: Cipher = Cipher.getInstance("AES/ECB/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE, skeySpec)
            val encrypted: ByteArray = cipher.doFinal(value)
            return Base64.encodeToString(encrypted, Base64.DEFAULT)
        } catch (ex: Exception) {
            ex.printStackTrace()
        }
        return null
    }

    @SuppressLint("GetInstance")
    fun decrypt(encrypted: String?,privateKey:String): ByteArray? {
        try {
//            val iv = IvParameterSpec(ivString.toByteArray(charset("UTF-8")))
            val skeySpec = SecretKeySpec(privateKey.toByteArray(charset("UTF-8")), "AES")
            val cipher: Cipher = Cipher.getInstance("AES/ECB/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, skeySpec)
            var original = ByteArray(0)
            original = cipher.doFinal(Base64.decode(encrypted, Base64.DEFAULT))
            return original
        } catch (ex: Exception) {
            ex.printStackTrace()
        }
        return null
    }

    companion object {
        var shared: AesEncryption? = null
            get() = if (field != null) {
                field
            } else {
                field = AesEncryption()
                field
            }
            private set
    }
}