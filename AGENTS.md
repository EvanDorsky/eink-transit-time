# Connecting Home Assistant to the transit display

A guide for setting up Home Assistant (HA) to work with this display, written
for an agent (or a person) doing it by hand. The display works without HA and
shows arrivals and, if `TRANSIT_LAT`/`TRANSIT_LON` are set at build time,
weather from Open-Meteo, which it fetches itself; HA doesn't supply weather.
HA adds notices in the side column, standby, and control of the display's
modes.

## How they talk

Everything goes over MQTT. The display connects to a broker at
`homeassistant.local:1883` (found by mDNS) with `MQTT_USER`/`MQTT_PASS` from
`include/secrets.h`.

1. Install the **Mosquitto broker** add-on (app) in HA and create an HA user
   for the display. The broker accepts HA users as MQTT logins.
2. Add the **MQTT** integration (Settings → Devices & services). Discovery is on
   by default, which is all the display needs.
3. Flash the display. Within a minute a **Transit Display** device appears with
   its entities. Nothing needs to be configured on the HA side for them.

## What the display creates in HA

All of these come from MQTT discovery. Their entity ids are stable, so refer to
them by entity id, not by friendly name: names follow the row labels.

| Entity | What it is |
|---|---|
| `sensor.transit_display_train_*`, `..._bus_*` | Minutes to the next train/bus per row |
| `sensor.transit_display_route_*` | Route letter currently shown per subway row (primary or fallback) |
| `switch.transit_display_commute_mode` | Commute mode allowed (big next-three-trains screen inside the window) |
| `number.transit_display_commute_start_hour` / `_end_hour` | Commute window, local hours (Mon–Fri) |
| `switch.transit_display_pause_updates` | Standby: blank panel, no fetching. The HOME key peeks for 5 min |
| `switch.transit_display_flight_mode` | Ranked routes to JFK instead of arrivals (also the EXIT button) |

**Control the display only through these entities.** Their commands are
published retained, so a change sticks across display reboots and reaches it
even if it was offline. Don't publish to the `.../set` topics yourself: a stray
retained message there silently overrides the entities on the next reconnect.

## What HA feeds the display

Two topics carry data that has no entity: the side-column **notices**, and
the Uber drive time for flight mode. Publish them **retained** (so the display
gets them as soon as it connects) from an automation with `mqtt.publish`. The
formats are also documented in `include/mqtt.h`.

| Topic | Payload | Shown as |
|---|---|---|
| `transit-display/notify` | `{"items":[{"title":"Printing","text":"1h20 left","pct":42}]}` | Side column notices (rotate if several) |
| `transit-display/flight/uber` | `23` | Flight mode only: the Uber row's drive minutes (`-1` or nothing = hidden) |

- The column is 92 px wide at 11 pt, so keep `title` and `text` to about 9
  characters. `pct` (0–100) adds a progress bar. An empty list clears the
  zone. Put every notice in **one** automation that rebuilds the whole list,
  since each publish replaces it.
- The panel is rated for about 1M refreshes and only redraws when what's shown
  changes. Don't publish values that change every few seconds (e.g. a notice
  with a seconds counter).

## Example automations

Shapes only: swap in your own entity ids.

**Notices** (one automation for all of them; this one shows a low battery):

```yaml
triggers:
  - trigger: state
    entity_id: sensor.remote_battery
  - trigger: homeassistant
    event: start
actions:
  - variables:
      items: >-
        {% set ns = namespace(items=[]) %}
        {% if has_value('sensor.remote_battery') and states('sensor.remote_battery') | int(101) <= 10 %}
          {% set b = states('sensor.remote_battery') | int %}
          {% set ns.items = ns.items + [{'title': 'Remote', 'text': b ~ '% left', 'pct': b}] %}
        {% endif %}
        {{ ns.items }}
  - action: mqtt.publish
    data:
      topic: transit-display/notify
      retain: true
      payload: "{{ {'items': items} | to_json }}"
mode: restart
```

**Standby** (saves panel refreshes: nights, and while nobody's home):

```yaml
triggers:
  - trigger: time
    at: ["01:00:00", "06:00:00"]
  - trigger: state
    entity_id: person.you
  - trigger: state
    entity_id: switch.transit_display_pause_updates
    from: unavailable      # display came back online: re-assert
  - trigger: homeassistant
    event: start
actions:
  - if:
      - or:
          - condition: time
            after: "01:00:00"
            before: "06:00:00"
          - condition: state
            entity_id: person.you
            state: not_home
    then:
      - action: switch.turn_on
        target: {entity_id: switch.transit_display_pause_updates}
    else:
      - action: switch.turn_off
        target: {entity_id: switch.transit_display_pause_updates}
mode: restart
```

Any other "nobody's looking" signal can be added as another condition, for
example all the lights being off at night. The `from: unavailable` trigger
matters: a switch command sent while the display is offline can be dropped, so
the automation re-asserts the state when it reconnects.

**Commute mode** needs no automation: turn the switch on and set the two hour
numbers. To pause it for a while, turn the switch off and schedule it back on
(e.g. an `input_datetime` plus a time trigger that calls `switch.turn_on`).

## Checking it works

- The device's entities are `unavailable` when the display is offline. They
  come back within a minute of it reconnecting.
- `python3 scripts/udplog.py` (or `make udplog`) shows the display's log on the
  LAN: `mqtt: connected`, `weather: ... (open-meteo)`, `notify: N item(s)`, `pause: on`, and
  a `draw: ...` line for every panel refresh.
- To see what the broker is holding for the display, call `mqtt.dump` with
  topic `transit-display/#`. HA writes the result to `mqtt_dump.txt` in its
  config directory.
