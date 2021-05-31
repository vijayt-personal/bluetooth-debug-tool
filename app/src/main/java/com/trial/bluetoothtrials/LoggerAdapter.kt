package com.trial.bluetoothtrials

import android.bluetooth.le.ScanResult
import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.cardview.widget.CardView
import androidx.recyclerview.widget.RecyclerView

class LoggerAdapter(val context:Context, private val dataSet: java.util.ArrayList<LogRecord>, val itemClick: ItemClick) :
        RecyclerView.Adapter<LoggerAdapter.ViewHolder>() {
    private lateinit var dataFilterSet: ArrayList<LogRecord>

    /**
     * Provide a reference to the type of views that you are using
     * (custom ViewHolder).
     */
    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val zoneStatus: TextView
        val locationA : TextView
        val distanceA:TextView
        val locationB: TextView
        val distanceB : TextView
        val locationC:TextView
        val distanceC: TextView
        val cardView: CardView
        val locationheader: TextView
        val distanceheader:TextView
        val locator_b_part:LinearLayout
        val locator_c_part:LinearLayout
        val distance_b_part:LinearLayout
        val distance_c_part:LinearLayout
        init {
            // Define click listener for the ViewHolder's View.
            zoneStatus = view.findViewById(R.id.zone_status)
            locationA = view.findViewById(R.id.locator_a_id)
            locationB = view.findViewById(R.id.locator_b_id)
            locationC = view.findViewById(R.id.locator_c_id)
            distanceA = view.findViewById(R.id.distance_a)
            distanceB = view.findViewById(R.id.distance_b)
            distanceC = view.findViewById(R.id.distance_c)
            cardView = view.findViewById(R.id.card_view)
            locationheader = view.findViewById(R.id.locator_header)
            distanceheader = view.findViewById(R.id.distance_header)
            locator_b_part = view.findViewById(R.id.locator_b_part)
            locator_c_part = view.findViewById(R.id.locator_c_part)
            distance_b_part = view.findViewById(R.id.distance_b_part)
            distance_c_part = view.findViewById(R.id.distance_b_part)

        }
    }

    // Create new views (invoked by the layout manager)
    override fun onCreateViewHolder(viewGroup: ViewGroup, viewType: Int): ViewHolder {
        // Create a new view, which defines the UI of the list item
        val view = LayoutInflater.from(viewGroup.context)
                .inflate(R.layout.log_item, viewGroup, false)

        return ViewHolder(view)
    }

    // Replace the contents of a view (invoked by the layout manager)
    override fun onBindViewHolder(viewHolder: ViewHolder, position: Int) {

        // Get element from your dataset at this position and replace the
        // contents of the view with that element
        val rawString=dataFilterSet[position].rawData
        val zoneStatus=rawString.subSequence(16,18)
        var zone:String=""
        if(zoneStatus.equals("04")){
            zone="Inside Zone at "+dataFilterSet[position].timestamp
            viewHolder.locationheader.text="Locator ID"
            viewHolder.distanceheader.text="Distance(m)"
            viewHolder.cardView.setCardBackgroundColor(context.getColor(R.color.red))
        }else if(zoneStatus.equals("05")){
            zone="Outside Zone at "+dataFilterSet[position].timestamp
            viewHolder.locationheader.text="Locator ID"
            viewHolder.distanceheader.text="Distance(m)"
            viewHolder.cardView.setCardBackgroundColor(context.getColor(R.color.verygreen))
        }else if(zoneStatus.equals("06")){
            zone="Ranging Failure at "+dataFilterSet[position].timestamp
            viewHolder.locationheader.text="Error Code"
            viewHolder.distanceheader.text="Failure Count"
            viewHolder.cardView.setCardBackgroundColor(context.getColor(R.color.blue))
            viewHolder.locator_b_part.visibility=View.GONE
            viewHolder.locator_c_part.visibility=View.GONE
            viewHolder.distance_b_part.visibility=View.GONE
            viewHolder.distance_c_part.visibility=View.GONE
            viewHolder.locationB.visibility=View.GONE
            viewHolder.locationC.visibility=View.GONE
            viewHolder.distanceB.visibility=View.GONE
            viewHolder.distanceC.visibility=View.GONE
        }else{
            zone="Unknown Status at"+dataFilterSet[position].timestamp
            viewHolder.locationheader.text="Locator ID"
            viewHolder.distanceheader.text="Distance(m)"
            viewHolder.cardView.setCardBackgroundColor(context.getColor(R.color.black))

        }
        viewHolder.zoneStatus.text = zone
        if(!zoneStatus.equals("06")){
//            viewHolder.locator_b_part.visibility=View.VISIBLE
//            viewHolder.distance_b_part.visibility=View.VISIBLE
//            viewHolder.locator_c_part.visibility=View.VISIBLE
//            viewHolder.distance_c_part.visibility=View.VISIBLE
            viewHolder.locator_b_part.visibility=View.VISIBLE
            viewHolder.locator_c_part.visibility=View.VISIBLE
            viewHolder.distance_b_part.visibility=View.VISIBLE
            viewHolder.distance_c_part.visibility=View.VISIBLE
            viewHolder.locationB.visibility=View.VISIBLE
            viewHolder.locationC.visibility=View.VISIBLE
            viewHolder.distanceB.visibility=View.VISIBLE
            viewHolder.distanceC.visibility=View.VISIBLE
        viewHolder.locationA.text = rawString.subSequence(26,34)
        viewHolder.distanceA.text = (Integer.decode("0x"+rawString.subSequence(36,38)+rawString.subSequence(34,36) as String).toString().toFloat()/100).toString()
        viewHolder.locationB.text = rawString.subSequence(38,46)
        viewHolder.locationC.text = rawString.subSequence(50,58)
        viewHolder.distanceB.text = (Integer.decode("0x"+rawString.subSequence(48,50)+rawString.subSequence(46,48) as String).toString().toFloat()/100).toString()
        viewHolder.distanceC.text = (Integer.decode("0x"+rawString.subSequence(60,62)+rawString.subSequence(58,60) as String).toString().toFloat()/100).toString()

        }else{
            viewHolder.locationA.text = rawString.subSequence(26,28)
            viewHolder.distanceA.text =rawString.subSequence(28,30)

        }
//        viewHolder.card.setOnClickListener(View.OnClickListener {
//
//            itemClick.onItemClick(dataSet[position])
//        })
    }
    init {
        dataFilterSet = dataSet
    }
    // Return the size of your dataset (invoked by the layout manager)
    override fun getItemCount(): Int {
        return dataFilterSet.size
    }
    fun bytesToHex(bytes: ByteArray): String {
        var hexString:String=""
        for (b in bytes) {
            val hexElement = String.format("%02X", b)
            hexString+=hexElement
        }
        return hexString
    }
}