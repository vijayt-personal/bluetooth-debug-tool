package com.trial.bluetoothtrials

import android.graphics.ColorSpace
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Filter
import android.widget.Filterable
import android.widget.TextView
import androidx.appcompat.widget.AppCompatButton
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.recyclerview.widget.RecyclerView

class CustomAdapter(private val dataSet: ArrayList<Device>,val itemClick: ItemClick) :
        RecyclerView.Adapter<CustomAdapter.ViewHolder>(),Filterable {
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
        val connect: AppCompatButton

        init {
            // Define click listener for the ViewHolder's View.
            deviceName = view.findViewById(R.id.device_name)
            deviceAddress = view.findViewById(R.id.device_address)
            scanRecord = view.findViewById(R.id.scan_record)
            card = view.findViewById(R.id.layout)
            rssi = view.findViewById(R.id.rssi)
            connect=view.findViewById(R.id.connect)
        }
    }

    // Create new views (invoked by the layout manager)
    override fun onCreateViewHolder(viewGroup: ViewGroup, viewType: Int): ViewHolder {
        // Create a new view, which defines the UI of the list item
        val view = LayoutInflater.from(viewGroup.context)
                .inflate(R.layout.device_list_item, viewGroup, false)

        return ViewHolder(view)
    }

    // Replace the contents of a view (invoked by the layout manager)
    override fun onBindViewHolder(viewHolder: ViewHolder, position: Int) {

        // Get element from your dataset at this position and replace the
        // contents of the view with that element
        viewHolder.deviceName.text = dataFilterSet[position].name
        viewHolder.deviceAddress.text = dataFilterSet[position].address
        viewHolder.scanRecord.text = dataFilterSet[position].scanHex.substring(0,62)
        viewHolder.rssi.text= dataFilterSet[position].rssi.toString()
        if(dataFilterSet[position].connected)
            viewHolder.connect.visibility=View.VISIBLE
        else
            viewHolder.connect.visibility=View.GONE
        viewHolder.connect.setOnClickListener(View.OnClickListener {

            itemClick.onItemClick(dataSet[position])
        })
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
                if (charSearch.isEmpty()||charSearch.equals("N/A,N/A,N/A")) {
                    dataFilterSet = dataSet
                } else {
                    var resultList = ArrayList<Device>()
                    for (row in dataFilterSet) {
                        if(!constraint.toString().split(",")[0].equals("N/A")){
                            if (row.name!!.toLowerCase().contains(constraint.toString().split(",")[0].toLowerCase())) {
                                resultList.add(row)
                            }
                        }

                    }
                    if(!resultList.isEmpty()) {
                        dataFilterSet = resultList

                    }
                    if(!constraint.toString().split(",")[1].equals("N/A")) {
                        resultList = ArrayList<Device>()
                        for (row in dataFilterSet) {

                            if (row.address!!.toLowerCase().contains(constraint.toString().split(",")[1].toLowerCase())) {
                                resultList.add(row)
                            }


                        }
                    }
                    if(!resultList.isEmpty()) {
                        dataFilterSet = resultList

                    }

                    if(!constraint.toString().split(",")[2].equals("N/A")){
                    resultList = ArrayList<Device>()
                    for (row in dataFilterSet) {

                            if (row.scanHex!!.toLowerCase().contains(constraint.toString().split(",")[2].toLowerCase())) {
                                resultList.add(row)
                            }


                    }
                    }
                    dataFilterSet = resultList
                }
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