package com.trial.bluetoothtrials

import android.content.Intent
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import androidx.core.content.ContextCompat
import java.util.*
import kotlin.concurrent.schedule

class SplashActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_splash)
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)

        Timer("SettingUp", false).schedule(3000) {
        doSomething()
        }
    }

    private fun doSomething() {
        val intent=Intent(this, DashBoardActivity  ::class.java)
        startActivity(intent)
        finish()
    }
}