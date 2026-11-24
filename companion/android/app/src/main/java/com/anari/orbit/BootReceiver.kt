package com.anari.orbit

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Picks the watch connection back up after the phone restarts or the app updates. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // The receiver is exported, so another app could aim any intent at it.
        if (intent.action == Intent.ACTION_BOOT_COMPLETED || intent.action == Intent.ACTION_MY_PACKAGE_REPLACED) {
            LinkService.start(context)
        }
    }
}
