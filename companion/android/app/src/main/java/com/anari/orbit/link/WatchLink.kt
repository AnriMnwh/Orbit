package com.anari.orbit.link

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.os.Handler
import android.util.Log
import org.json.JSONObject

/**
 * The Bluetooth connection to the watch.
 *
 * Everything here runs on [handler]'s thread: Android's callbacks are
 * delivered there, and callers must call in from there too. Android allows
 * one Bluetooth operation at a time, so writes wait in two queues; control
 * lines always go ahead of bulk frames.
 */
@SuppressLint("MissingPermission") // The service checks BLUETOOTH_CONNECT before it creates the link.
class WatchLink(
    private val context: Context,
    private val handler: Handler,
    private val events: Events,
) {
    interface Events {
        /** Connected and listening; lines can flow. */
        fun onUp()
        fun onDown()
        fun onLine(json: JSONObject)
        fun onBulk(frame: ByteArray)
    }

    private sealed interface Op
    private class Subscribe(val characteristic: BluetoothGattCharacteristic) : Op
    private class Write(val characteristic: BluetoothGattCharacteristic, val data: ByteArray, val wait: Boolean) : Op
    private class Then(val run: () -> Unit) : Op

    private var address: String? = null
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var bulkIn: BluetoothGattCharacteristic? = null
    private val control = ArrayDeque<Op>()
    private val bulk = ArrayDeque<Op>()
    private var inFlight: ArrayDeque<Op>? = null
    private var retries = 0

    var up = false
        private set
    var mtu = 23
        private set

    /** Whether this watch offers the bulk channel (older firmware does not). */
    val hasBulk get() = bulkIn != null

    /**
     * Set by the owner once the watch has answered the app's hello, which it
     * does only on an encrypted link. Before that the watch throws bulk frames
     * away without saying so, so none are sent.
     */
    var bulkReady = false

    private val lines = LineReader { text ->
        try {
            events.onLine(JSONObject(text))
        } catch (e: Exception) {
            Log.w(TAG, "unreadable line from watch: ${text.take(80)}")
        }
    }

    /** Connects and keeps reconnecting until [close]. [quick] trades battery for a faster first connection. */
    fun connect(address: String, quick: Boolean) {
        close()
        this.address = address
        open(quick)
    }

    fun close() {
        address = null
        handler.removeCallbacksAndMessages(TOKEN)
        drop()
    }

    private fun open(quick: Boolean) {
        val address = address ?: return
        val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter ?: return
        if (!adapter.isEnabled) return // the service calls connect() again when Bluetooth comes back
        try {
            val device = adapter.getRemoteDevice(address)
            // The newer way to connect needs a later Android than the oldest this app supports.
            @Suppress("DEPRECATION")
            gatt = device.connectGatt(context, !quick, callback, BluetoothDevice.TRANSPORT_LE, BluetoothDevice.PHY_LE_1M_MASK, handler)
        } catch (e: Exception) {
            Log.w(TAG, "could not start connecting: $e")
        }
    }

    /** Lets go of the current connection, telling the owner if it was up. */
    private fun drop() {
        val wasUp = up
        up = false
        bulkReady = false
        mtu = 23
        rx = null
        bulkIn = null
        control.clear()
        bulk.clear()
        inFlight = null
        retries = 0
        lines.reset()
        gatt?.let {
            try {
                it.disconnect()
                it.close()
            } catch (e: Exception) {
                Log.w(TAG, "closing: $e")
            }
        }
        gatt = null
        if (wasUp) events.onDown()
    }

    /** A fresh connection attempt is the one recovery that works on every phone. */
    private fun reopenSoon() {
        drop()
        handler.removeCallbacksAndMessages(TOKEN)
        handler.postDelayed({ open(false) }, TOKEN, 1500)
    }

    // ---------------------------------------------------------------- sending

    fun send(json: JSONObject) = sendRaw(Link.line(json))

    fun sendRaw(line: ByteArray) {
        val rx = rx ?: return
        if (!up) return
        if (line.size > Link.MAX_LINE) {
            Log.w(TAG, "line of ${line.size} bytes is too long for the watch; dropped")
            return
        }
        for (piece in Link.split(line, mtu - 3)) control += Write(rx, piece, wait = true)
        pump()
    }

    /**
     * Queues frames for the bulk channel and runs [done] once the last one has
     * left. Returns false, sending nothing, while the channel is not usable.
     */
    fun sendBulk(frames: List<ByteArray>, done: () -> Unit = {}): Boolean {
        val bulkIn = bulkIn
        if (!up || bulkIn == null || !bulkReady) return false
        // A short burst at the fastest connection rate, then back to the frugal one.
        bulk += Then { gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH) }
        for (frame in frames) bulk += Write(bulkIn, frame, wait = false)
        bulk += Then {
            gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_BALANCED)
            done()
        }
        pump()
        return true
    }

    /** Forgets bulk frames not yet sent (the assistant was cancelled, say). */
    fun clearBulk() {
        if (inFlight === bulk) {
            val current = bulk.first()
            bulk.clear()
            bulk += current
        } else {
            bulk.clear()
        }
    }

    private fun pump() {
        val gatt = gatt ?: return
        while (inFlight == null) {
            val queue = if (control.isNotEmpty()) control else bulk
            val started = when (val op = queue.firstOrNull() ?: return) {
                is Then -> {
                    queue.removeFirst()
                    op.run()
                    continue
                }
                is Subscribe -> {
                    val descriptor = op.characteristic.getDescriptor(Link.CCCD)
                    if (descriptor == null) {
                        queue.removeFirst()
                        continue
                    }
                    gatt.setCharacteristicNotification(op.characteristic, true)
                    gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                }
                is Write -> gatt.writeCharacteristic(
                    op.characteristic, op.data,
                    if (op.wait) BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT else BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE,
                )
            }
            if (started == BluetoothStatusCodes.SUCCESS) {
                inFlight = queue
                retries = 0
            } else if (++retries > 200) {
                Log.w(TAG, "the phone keeps refusing writes ($started); reconnecting")
                reopenSoon()
            } else {
                handler.postDelayed({ pump() }, TOKEN, 25)
            }
            return
        }
    }

    private fun finished() {
        inFlight?.removeFirstOrNull()
        inFlight = null
        pump()
    }

    // --------------------------------------------------------------- callbacks

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g !== gatt) return
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (!g.requestMtu(517)) g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "disconnected ($status)")
                reopenSoon()
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, value: Int, status: Int) {
            if (g !== gatt) return
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = value
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (g !== gatt) return
            val serial = g.getService(Link.NUS)
            val tx = serial?.getCharacteristic(Link.NUS_TX)
            rx = serial?.getCharacteristic(Link.NUS_RX)
            if (status != BluetoothGatt.GATT_SUCCESS || tx == null || rx == null) {
                // Most likely the watch is being a keyboard or an instrument right now.
                Log.w(TAG, "the watch's phone link is not on offer ($status)")
                reopenSoon()
                return
            }
            control += Subscribe(tx)
            val bulkService = g.getService(Link.BULK)
            bulkIn = bulkService?.getCharacteristic(Link.BULK_IN)
            bulkService?.getCharacteristic(Link.BULK_OUT)?.let { control += Subscribe(it) }
            control += Then {
                up = true
                events.onUp()
            }
            pump()
        }

        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (g === gatt) finished()
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (g === gatt) finished()
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            if (g !== gatt) return
            when (characteristic.uuid) {
                Link.NUS_TX -> lines.feed(value)
                Link.BULK_OUT -> events.onBulk(value)
            }
        }
    }

    private companion object {
        const val TAG = "OrbitLink"
        val TOKEN = Any()
    }
}
