package com.trial.bluetoothtrials.Utility

import android.content.Context

open class LoadingUtils {

    companion object {
        private var appLoader: AppLoader? = null
        fun showDialog(
                context: Context?,
                isCancelable: Boolean
        ) {
            hideDialog()
            if (context != null) {
                try {
                    appLoader = AppLoader(context)
                    appLoader?.let { appLoader->
                        appLoader.setCanceledOnTouchOutside(true)
                        appLoader.setCancelable(isCancelable)
                        appLoader.show()
                    }

                } catch (e: Exception) {
                    e.printStackTrace()
                }
            }
        }

        fun hideDialog() {
            if (appLoader !=null && appLoader?.isShowing!!) {
                appLoader = try {
                    appLoader?.dismiss()
                    null
                } catch (e: Exception) {
                    null
                }
            }
        }

    }


}