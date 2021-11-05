package com.trial.bluetoothtrials

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import kotlinx.android.synthetic.main.activity_logger.*

class LoggerActivity : AppCompatActivity(), ItemClick {
    private lateinit var adapter:LoggerAdapter
    private lateinit var linearLayoutManager: LinearLayoutManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_logger)
        val extras = intent.extras
        val arraylist  = extras.getParcelableArrayList<LogRecord>("logList")
        arraylist.reverse()
        window.statusBarColor = ContextCompat.getColor(this, R.color.black)
        linearLayoutManager = LinearLayoutManager(this)
        log_list.layoutManager = linearLayoutManager
        adapter = LoggerAdapter(applicationContext,arraylist, this)
        log_list.adapter = adapter

    }

    override fun onItemClick(device: Device) {
        TODO("Not yet implemented")
    }
}