package com.trial.bluetoothtrials.Utility;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.util.Log;
import android.view.Gravity;
import android.widget.Toast;



import java.io.IOException;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static android.content.Context.WIFI_SERVICE;

/**
 * Created by yadhukrishnan.e@oneteamus.com
 */
//Util class is a collection of some commonly used functions.
public class Util {




    public static void showToastAtCenter(Context ctx, String message) {
        Toast toast = Toast.makeText(ctx,message, Toast.LENGTH_LONG);
        toast.setGravity(Gravity.CENTER, 0, 0);
        toast.show();
    }

    public static void showMessage(Context context, String message) {
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show();
    }

    public static void showMessage(Context context, int message) {
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show();
    }
    public static String formatDate(String date) {
        try {
            String[] splitDate = date.split("T");
            Date date1 = new SimpleDateFormat("yyyy-MM-dd").parse(splitDate[0]);
            DateFormat targetFormat = new SimpleDateFormat("MM/dd/yyyy");
            return targetFormat.format(date1);
            //"2018-08-30T18:30:00-04:00"

        } catch (Exception e) {
            e.printStackTrace();
        }
        return null;
    }



    //Progress dialog handling functions.

//    public static  boolean isMyAppLauncherDefault(Context context) {
//        Intent intent = new Intent(Intent.ACTION_MAIN);
//        intent.addCategory(Intent.CATEGORY_HOME);
//        ResolveInfo resolveInfo = null;
//        try {
//            resolveInfo = context.getPackageManager().resolveActivity(intent, PackageManager.COMPONENT_ENABLED_STATE_DEFAULT); //can return null!
//            if(resolveInfo!=null) {
//                Log.d("DEFAULT LAUNCHER","com.atcolauncher_prod.app");
//                if (resolveInfo.activityInfo.packageName.equalsIgnoreCase("com.atcolauncher_prod.app")||resolveInfo.activityInfo.packageName.equalsIgnoreCase("com.atcolauncher_dev.app")) {
//                    return true;
//                } else {
//                    return false;
//                }
//            }else{
//                return false;
//            }
//        }catch(RuntimeException e){
//            return false;
//        }
//    }




    //Gets package name.
    public static String getPackageName(String appName){
        String uri = null;
        if(appName.contains("TOOLKIT")){
            uri = "com.atco.toolkit";
        }
        return uri;
    }
    //Checks if app installed or not.
    public static boolean ifAppInstalled(Context context, String uri){
        PackageManager pm = context.getPackageManager();
        try {
            pm.getPackageInfo(uri, PackageManager.GET_ACTIVITIES);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        }catch (Exception e){

        }
        return false;
    }
    //Gets App versions.
    public static String getVersion(Context context, String uri){
        String version="";

        try {
            PackageInfo pInfo = context.getPackageManager().getPackageInfo(uri, 0);
            version = pInfo.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        } catch (Exception e){

        }
        return version;
    }
    //Gets App version codes.
    public static int getVersionCode(Context context, String uri){
        int version=0;

        try {
            PackageInfo pInfo = context.getPackageManager().getPackageInfo(uri, 0);
            version = pInfo.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        } catch (Exception e){

        }
        return version;
    }
    //Show permission alert to go to settings to enable permissions.

    //Shows an alert.
    public static void showAlert(Context context, String message, DialogInterface.OnClickListener listener ) {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setMessage(message);
        builder.setPositiveButton(android.R.string.ok, listener);
        builder.setNegativeButton(android.R.string.cancel, listener);
        AlertDialog dialog = builder.create();
        dialog.show();
        dialog.setCancelable(false);
    }

    //Shows a single alert.
    public static void showSingleAlert(Context context, String message, DialogInterface.OnClickListener listener ) {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setMessage(message);
        builder.setPositiveButton(android.R.string.ok, listener);

        AlertDialog dialog = builder.create();
        dialog.show();
        dialog.setCancelable(false);
    }
    //Shows an alert when log out happens.
    public static void showLogOutAlert(Context context, String message, DialogInterface.OnClickListener listener ) {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setMessage(message);
        builder.setPositiveButton("Yes", listener);
        builder.setNegativeButton("No", listener);
        AlertDialog dialog = builder.create();
        dialog.show();
        dialog.setCancelable(false);
    }
    //Shows an alert when toolkit has an update.
    public static void showToolKitUpdateAlert(Context context, String message, DialogInterface.OnClickListener listener ) {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setMessage(message);
        builder.setPositiveButton(android.R.string.ok, listener);

        AlertDialog dialog = builder.create();
        dialog.show();
        dialog.setCancelable(false);
    }


    public static boolean isOnline() {

        Runtime runtime = Runtime.getRuntime();
        try {

            Process ipProcess = runtime.exec("/system/bin/ping -c 1 8.8.8.8");
            int     exitValue = ipProcess.waitFor();
//            Log.d("Exit value ",exitValue+"");
            return (exitValue == 0);

        } catch (IOException e)          { e.printStackTrace(); }
        catch (InterruptedException e) { e.printStackTrace(); }
//        Log.d("False","Called");
        return false; }


}
