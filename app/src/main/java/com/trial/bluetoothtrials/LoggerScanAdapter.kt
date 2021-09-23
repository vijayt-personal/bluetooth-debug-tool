package com.trial.bluetoothtrials

import android.os.Handler
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Filter
import android.widget.Filterable
import android.widget.TextView
import android.widget.Toast
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.recyclerview.widget.RecyclerView

class LoggerScanAdapter(private val dataSet: ArrayList<Device>, val itemClick: ItemClick) :
        RecyclerView.Adapter<LoggerScanAdapter.ViewHolder>(),Filterable {
    private lateinit var dataFilterSet: ArrayList<Device>

    /**
     * Provide a reference to the type of views that you are using
     * (custom ViewHolder).
     */
    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val deviceName: TextView
        val deviceAddress: TextView
        val scanRecord:TextView
        val card:ConstraintLayout
        val rssi: TextView


        init {
            // Define click listener for the ViewHolder's View.
            deviceName = view.findViewById(R.id.device_name)
            deviceAddress = view.findViewById(R.id.device_address)
            scanRecord = view.findViewById(R.id.scan_record)
            card = view.findViewById(R.id.layout)
            rssi = view.findViewById(R.id.rssi)

        }
    }

    // Create new views (invoked by the layout manager)
    override fun onCreateViewHolder(viewGroup: ViewGroup, viewType: Int): ViewHolder {
        // Create a new view, which defines the UI of the list item
        val view = LayoutInflater.from(viewGroup.context)
                .inflate(R.layout.log_device_list_item, viewGroup, false)

        return ViewHolder(view)
    }

    // Replace the contents of a view (invoked by the layout manager)
    override fun onBindViewHolder(viewHolder: ViewHolder, position: Int) {

        // Get element from your dataset at this position and replace the
        // contents of the view with that element
        if(position<dataFilterSet.size) {

            viewHolder.deviceName.text = dataFilterSet[position].scanHex.substring(18, 26).toLong(16).toString()
            viewHolder.deviceAddress.text = dataFilterSet[position].address
            viewHolder.scanRecord.text = dataFilterSet[position].scanHex.substring(0, 62)
            viewHolder.rssi.text = dataFilterSet[position].rssi.toString()

            viewHolder.card.setOnClickListener(View.OnClickListener {

                itemClick.onItemClick(dataSet[position])
            })
        }
    }
    init {
        dataFilterSet = dataSet
    }
    // Return the size of your dataset (invoked by the layout manager)
    override fun getItemCount(): Int {
        return dataFilterSet.size
    }
    override fun getFilter(): Filter {
        return object : Filter() {
            override fun performFiltering(constraint: CharSequence?): FilterResults {
                val charSearch = constraint.toString()
                dataFilterSet=dataSet
                var resultList = ArrayList<Device>()



                    resultList = ArrayList<Device>()
                    for (row in dataFilterSet) {

                            if (row.scanHex!!.toLowerCase().contains("970152")) {
                                resultList.add(row)
                            }


                    }

                    dataFilterSet = resultList

                val filterResults = FilterResults()
                filterResults.values = dataFilterSet
                return filterResults
            }

            override fun publishResults(constraint: CharSequence?, results: FilterResults?) {

                if (results != null)
                    dataFilterSet = results.values as ArrayList<Device>

                notifyDataSetChanged()
            }
        }
    }



}