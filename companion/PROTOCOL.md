# Orbit Link, version 1 (draft)

How the Orbit phone app and the watch talk over Bluetooth LE.

Status: both sides of everything in this file are written and compile. The
watch's hello lists ten names: `art`, `music2`, `agent`, `place`, `agenda`,
`photos`, `cfg`, `act`, `replies`, `phone`. Nothing has run on a real watch,
and the two sides have never talked to each other. One writer only: the app
session edits this file; the firmware session asks for changes by message.

## Principles

- The app extends the link Gadgetbridge already uses, so the watch keeps one
  parser and Gadgetbridge keeps working when the app is not installed.
- Everything new is additive. A watch or app that does not know a message
  ignores it.
- The app is optional. With no app the iPhone services and Gadgetbridge
  behave exactly as they do today.

## Two channels

| Channel | Where | Carries |
| --- | --- | --- |
| Control | Nordic UART service `6e400001-b5a3-f393-e0a9-e50e24dcca9e` (exists) | Lines of text, as today |
| Bulk | New service `4f524249-5400-4c4e-8b00-000000000001` | Binary frames: album art, speech |

Bulk characteristics, visible only in the "Orbit" phone identity:

| UUID | Direction | Properties |
| --- | --- | --- |
| `4f524249-5400-4c4e-8b00-000000000002` | phone to watch | write without response |
| `4f524249-5400-4c4e-8b00-000000000003` | watch to phone | notify |

The watch starts pairing as soon as it sees the app's hello (it does not for
Gadgetbridge, which never bonds). The phone-to-watch bulk characteristic needs
an encrypted link. Because it is written without a response, a frame sent
before encryption is up is thrown away with no error. The rule that makes this
safe: the watch answers the hello only once the link is encrypted, and the app
sends nothing bulk-related, not even a `blob` line, until it has that answer.

The app asks for the largest packet size; the watch accepts up to 256 bytes
and never asks first. Assume the slow 1M radio mode until measured.

### Control lines

Unchanged from the Bangle.js protocol Gadgetbridge speaks:

- Phone to watch: `\x10GB({...json...})\n`, and `\x10setTime(utc);E.setTimeZone(hours);\n`.
- Watch to phone: one JSON object per line, ending `\n`.
- Strings are JSON-escaped UTF-8. A line to the watch is at most 1000 bytes
  (the watch's line buffer is 1500).

### Bulk frames

One write or notification is one frame:

```
byte 0   kind      1 album art, 2 microphone speech, 3 reply speech
byte 1   sequence  counts up from 0 for each stream, wraps at 255
byte 2.. payload   up to (MTU - 5) bytes
```

A stream is opened and closed by control lines (below). A receiver that sees
a gap in the sequence drops that stream and says so on the control channel.
Kind 16 is a photo for the watch's library; 17 and up are reserved for map
tiles and firmware.

## Messages

### Hello

The app sends this first, straight after subscribing. It is how the watch
tells the app from Gadgetbridge.

```
phone: GB({"t":"orbit","v":1,"app":"android 0.1","caps":["art","music2","agent"]})
watch: {"t":"orbit","v":1,"fw":"Orbit 0.9","caps":["art","music2","agent"],
        "art":{"w":240,"h":240,"max":20000},"mtu":256}
```

`caps` lists what each side can do; a feature is used only when both list it.
`art` is the picture the watch wants: size in pixels and the largest file in
bytes.

- On the hello the watch asks to pair, then answers at once if the link is
  already encrypted, otherwise when encryption comes up. Its answer is two
  lines: this one, then the assistant's tool list (see below).
- Until the watch answers, each side treats the other as plain Gadgetbridge:
  no extras in either direction.
- If pairing is declined or fails the watch never answers on that connection.
  Sending the hello again makes it ask to pair again; the app offers that as
  "Ask to pair again" and never repeats the hello by itself.
- A disconnect clears a hello that is still waiting. The app says hello afresh
  on every connection.

### Everything Gadgetbridge already does

The app sends and understands the same messages the firmware handles today:
`notify`, `notify-`, `call`, `musicinfo`, `musicstate`, `gps`, `weather`,
`find`, `is_gps_active` to the watch; `ver`, `status`, `music`, `notify`
(`DISMISS`, `REPLY`), `call`, `findPhone`, `gps_power` from it.

One extra field on `notify`: `"reply":true` when the notification accepts a
typed reply, so the watch can offer its quick replies only where they work.

### Richer playback (`music2`)

Phone to watch, extra fields on the existing messages:

```
GB({"t":"musicinfo","artist":"..","album":"..","track":"..","dur":215,
    "app":"Spotify","art":"3fa9c1d2"})
GB({"t":"musicstate","state":"play","position":42,"shuffle":1,"repeat":0,
    "vol":63,"can":["seek","shuffle","repeat","like"]})
```

- `art` is a short key for this track's picture; `""` means there is none and
  the watch clears what it shows.
- `vol` is 0 to 100. `can` lists the extra controls the current player accepts.

Watch to phone, new values of `n` on the existing `music` message:

```
{"t":"music","n":"seek","pos":95}
{"t":"music","n":"vol","v":40}
{"t":"music","n":"shuffle"}   {"t":"music","n":"repeat"}   {"t":"music","n":"like"}
```

`pos` is the place to jump to, in seconds from the start. Once the app's hello
has been seen the watch's volume buttons send `vol` with the level they want,
and these five are never sent to Gadgetbridge.

### Album art (`art`)

The watch keeps one picture. The phone sends a picture after a `musicinfo`
whose `art` key differs from the last one it delivered on this connection.

```
phone: GB({"t":"blob","kind":"art","key":"3fa9c1d2","len":18234,"crc":"9a3f00c1"})
phone: bulk frames, kind 1, until len bytes are sent
watch: {"t":"blob","kind":"art","key":"3fa9c1d2","ok":true}
```

The picture is a baseline (not progressive) JPEG, 4:2:0, at exactly the size
the watch asked for in its hello. `crc` is CRC-32 (the zlib one) of the file,
in hex. Keys are letters and digits, at most 15.

- A missing frame, a wrong checksum or size, or a picture that will not decode
  gets `ok:false` at once, and the rest of that stream is ignored. The phone
  may send it once more.
- The watch does not time out a half-received picture. Silence means the
  frames never arrived; a new `blob` line starts again. The app tries three
  times, 20 seconds apart, then leaves that picture alone.
- The watch shows its one picture only while its key equals the latest
  `musicinfo` key, so returning to that track needs no resend.

### Assistant through the phone (`agent`)

The watch records, the phone does the internet work with the key saved in the
app, and the watch shows and plays the answer. The watch falls back to its
own Wi-Fi path when the app is not connected.

```
watch: {"t":"agent","n":"start","rate":16000,"codec":"ima"}
watch: bulk frames, kind 2, while the user speaks or all together afterwards
watch: {"t":"agent","n":"stop"}            or  {"t":"agent","n":"cancel"}

phone: GB({"t":"agent","n":"heard","text":"set a timer for five minutes"})
phone: GB({"t":"agent","n":"tool","id":1,"name":"start_timer","args":{"seconds":300}})
watch: {"t":"agent","n":"result","id":1,"out":"Timer started."}
phone: GB({"t":"agent","n":"reply","text":"Five minutes, starting now.","more":false})
phone: GB({"t":"agent","n":"speak","rate":16000,"codec":"ima","len":41200})
phone: bulk frames, kind 3
phone: GB({"t":"agent","n":"done"})
phone: GB({"t":"agent","n":"error","text":"No internet on the phone."})   instead, on failure
```

- The watch owns the list of tools. Once per connection it sends
  `{"t":"agent","n":"tools","tools":[...]}` with the same JSON it gives OpenAI
  today, so the app never has to know what the watch can do.
- A long `reply` comes as several lines with `"more":true` on all but the last.
- `speak` is optional. Without it the watch shows the text only. `len` is the
  total of the frame payloads that follow, so the watch can reserve one buffer
  and play it when complete.
- `stop` marks the end of the question, so the watch may record into its one
  buffer as it does today and send the frames once recording ends.
- The watch listens for `agent` lines only while a question is in flight, and
  gives up 60 seconds after the last one it heard. No single step on the phone
  may run longer than that without a line (`heard`, `tool`, `reply`) going out.
- Reply speech of more than 327680 bytes is not played; the app sends `done`
  without it. `rate` may be 8000 to 48000.
- If a reply-speech frame goes missing the watch drops the clip and says
  nothing; the words are already on its screen.

Speech codec `ima`: IMA ADPCM, 4 bits a sample, mono. Every bulk frame stands
alone so a lost frame cannot spoil the next:

```
bytes 0-1  predictor, signed 16-bit little-endian
byte  2    step index, 0 to 88
byte  3    0
bytes 4..  samples, two per byte, low half first
```

The header is the coder's state before the first sample of the frame, and
every half-byte yields one sample, so a frame always holds an even number.

At 16 kHz that is 8 kB a second, which a Bluetooth LE link carries with room
to spare.

## More the app and watch can do

Each part is switched on by the name in brackets appearing in both hellos.
The watch sends `cfg` and `act` only if the app's hello lists them; it accepts
everything phone-to-watch whenever it arrives.

Weather needs nothing new: the app sends Gadgetbridge's `weather` line, which
the watch reads today. It is off until the user turns it on in the app.

### Where the phone is (`place`)

```
phone: GB({"t":"place","lat":51.51,"lon":-0.13})
```

Rounded to two decimal places (about a kilometre), sent on connecting and
hourly while weather is on. For sunrise and sunset without opening Maps. The
watch ignores a position outside -90..90 and -180..180, saves one only when it
differs from what it has, and prefers a live fix while Maps is streaming.

### Calendar (`agenda`)

```
phone: GB({"t":"agenda","i":0,"n":2,"s":1794300000,"e":1794303600,"title":"Dentist","loc":"High St","allday":false})
phone: GB({"t":"agenda","i":1,"n":2,"s":1794310000,"e":1794313600,"title":"Band practice","loc":"","allday":false})
```

The next entries within two days, soonest first, at most eight, one to a line.
`i` is the entry's place in a list of `n`; a line with `i` 0 replaces the old
list, and `"n":0` means there is nothing coming up. `s` and `e` are seconds
since 1970 UTC. Titles are at most 40 characters and places 30. Sent on
connecting, hourly, and when the phone's calendar changes.

- Lines must arrive in order: the watch keeps an entry only if its `i` is the
  number it already holds, and drops the rest until the next `i` 0.
- A timed entry is a real moment; the watch shows it in its own time zone.
- An all-day entry is a date, not a moment: `s` is midnight UTC on its first
  date and `e` midnight UTC after its last, wherever the phone is (Android's
  convention). The watch dates it by reading `s` as UTC with no shift. The
  app decides which all-day entries to send by the date where the phone is.
- The watch drops a timed entry from its screen once `e` has passed. An
  all-day entry counts as on now from the watch's local date of `s` read as
  UTC, and ends when the watch's local date reaches the date of `e` read as
  UTC. An all-day entry whose `e` is not after `s` is treated as one day long.
- The watch keeps the list through a disconnect and loses it on restart. Its
  font is Latin only.

### Photos over Bluetooth (`photos`)

The phone cuts each picture to 466 x 466 and compresses it to a baseline JPEG
of at most 100000 bytes, so the watch stores it as it arrives.

```
phone: GB({"t":"blob","kind":"photo","key":"8ef26a33","len":41674,"crc":"8ef26a33"})
phone: bulk frames, kind 16
watch: {"t":"blob","kind":"photo","key":"8ef26a33","ok":true,"name":"p0007.jpg"}
phone: GB({"t":"photos","n":"delete","name":"p0007.jpg"})
```

One photo at a time; the phone waits for the reply before the next. `name` is
the file name the watch gave it, which the phone uses later to delete it.

- `len` is 200 to 100000 and the file must be a baseline JPEG of exactly
  466 x 466. The library holds 60 photos, shared with any added from the
  watch's own setup page, which the phone is not told about.
- The reply comes once the file is saved, which can take a second or two. As
  with album art there is no timeout on a half-received photo.
- `"ok":false` always says `why`, and the app acts on it:

| `why` | Meaning | The app |
| --- | --- | --- |
| `lost`, `busy` | Arrived damaged, or the last one is still being saved | Tries that photo once more, then waits for the next connection |
| `bad`, `size` | Not a picture the watch can use | Marks it "Not accepted" and moves on |
| `full`, `space`, `storage`, `memory` | No room, or the watch is in trouble | Stops and tells the user; tries again after they add or remove a photo, or on the next connection |

- `delete` gets no reply, and a name the watch does not know is ignored.

### Watch settings from the phone (`cfg`)

```
watch: {"t":"cfg","brightness":70,"dnd":false,"h24":true,"raise":true,"sound":true,"aod":1,"timeout":15,"goal":8000}
phone: GB({"t":"cfg","set":{"brightness":40}})
```

The watch always sends all eight: after its hello, whenever one changes on the
watch, and after every `set` (even one that changed nothing), so the app is
corrected at once if a change did not take. The phone changes one at a time,
though several in one `set` are all applied. `aod` is 0 off, 1 clock, 2
photos; `timeout` is seconds; `goal` is steps a day. Limits: brightness 5 to
100, timeout 5 to 120, goal 1000 to 60000.

### Step history (`act`)

```
watch: {"t":"act","steps":2850,"day":"2026-11-10"}
```

Today's count so far: after the hello (once the watch knows the time), on
each hour, and at 23:59 so the phone gets the day's total. The count restarts
at the watch's local midnight. The phone keeps the highest count seen for each
day and shows the week. `day` is the watch's local date.

### Quick replies (`replies`)

```
phone: GB({"t":"replies","list":["OK","On my way","Can't talk now","Call you later","Thanks"]})
```

Up to five, each at most 24 characters, replacing the watch's built-in ones.
An empty list brings the built-in ones back. Sent on connecting and when the
user edits them; the watch keeps them until it restarts.

### Phone battery (`phone`)

```
phone: GB({"t":"phone","bat":80,"chg":false})
```

On connecting, then when it moves by five percent or charging starts or stops.
`bat` is 0 to 100 and required. The watch forgets it on disconnect.

## Later

Map tiles and firmware updates over the bulk channel; GPS, weather and
find-my-phone for iPhone through an iPhone app that speaks this same protocol
next to Apple's own services.
