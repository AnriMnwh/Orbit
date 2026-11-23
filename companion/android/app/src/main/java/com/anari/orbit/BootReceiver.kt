package com.anari.orbit

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Picks the watch connection back up after the phone restarts or the app updates. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        LinkService.start(context)
    }
}
