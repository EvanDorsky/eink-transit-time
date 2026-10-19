# Connecting Home Assistant to the transit display

A guide for setting up Home Assistant (HA) to work with this display, written
for an agent (or a person) doing it by hand. The display works without HA and
shows arrivals only. HA adds weather, rain alerts, notices, the hourly
screen, standby, and control of its modes.

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

Four topics carry data that has no entity. Publish each one **retained** (so
the display gets it as soon as it connects) from an automation with
`mqtt.publish`. Formats are also documented in `include/mqtt.h`.

| Topic | Payload | Shown as |
|---|---|---|
| `transit-display/weather` | `{"cond":"sunny","hi":81,"lo":63,"rain_in":5,"rain_at":"6pm"}` | Side column: icon, today's hi-lo, and an umbrella + time when rain is coming |
| `transit-display/notify` | `{"items":[{"title":"Printing","text":"1h20 left","pct":42}]}` | Side column notices (rotate if several) |
| `transit-display/hourly` | `{"h":[{"t":"1pm","c":"rainy","f":75,"p":40}, ...]}` | HOME-key screen: next 8 hours |
| `transit-display/flight/uber` | `23` | Flight mode: the Uber row's drive minutes (`-1` or nothing = hidden) |

Field notes:

- `cond` and `c` are HA weather condition strings (`sunny`, `clear-night`,
  `partlycloudy`, `cloudy`, `rainy`, `pouring`, `snowy`, `snowy-rainy`, `hail`,
  `lightning`, `lightning-rainy`, `fog`, `windy`, `exceptional`), so pass the
  weather entity's state straight through. Temperatures are whole °F.
- `rain_in` is hours until the first wet hour (0 = raining now). Omit both rain
  keys, or send `rain_in` < 0, for "no rain coming".
- Notices: the column is 92 px wide at 11 pt, so keep `title` and `text` to
  about 9 characters. `pct` (0–100) adds a progress bar. An empty list clears
  the zone. Put every notice in **one** automation that rebuilds the whole list,
  since each publish replaces it.
- The panel is rated for about 1M refreshes and only redraws when what's shown
  changes. Don't publish values that change every few seconds (e.g. a notice
  with a seconds counter).

## Example automations

Shapes only: swap in your own entity ids. `weather.home` is any HA weather
entity. The hourly screen wants one whose hourly forecast includes
`precipitation_probability` (NWS does; met.no doesn't).

**Weather** (every 15 min, on change, and at startup):

```yaml
triggers:
  - trigger: state
    entity_id: weather.home
  - trigger: time_pattern
    minutes: /15
  - trigger: homeassistant
    event: start
actions:
  - action: weather.get_forecasts
    target: {entity_id: weather.home}
    data: {type: daily}
    response_variable: fc
  - action: weather.get_forecasts
    target: {entity_id: weather.home}
    data: {type: hourly}
    response_variable: fh
  - variables:
      day: "{{ fc['weather.home'].forecast[0] }}"
      wet_hours: >-
        {{ fh['weather.home'].forecast[:13]
           | selectattr('precipitation', 'defined')
           | selectattr('precipitation', 'gt', 0) | list }}
  - action: mqtt.publish
    data:
      topic: transit-display/weather
      retain: true
      payload: >-
        {% set w = wet_hours[0] if wet_hours else none %}
        {{ {'cond': states('weather.home'),
            'hi': day.temperature | round | int,
            'lo': day.templow | round | int,
            'rain_in': ((as_datetime(w.datetime) - now()).total_seconds() / 3600)
                       | round(0, 'ceil') | int if w else -1,
            'rain_at': (as_datetime(w.datetime) | as_local).strftime('%-I%p') | lower
                       if w else ''} | to_json }}
mode: single
```

**Hourly screen** (every 20 min):

```yaml
triggers:
  - trigger: time_pattern
    minutes: /20
  - trigger: homeassistant
    event: start
actions:
  - action: weather.get_forecasts
    target: {entity_id: weather.home}
    data: {type: hourly}
    response_variable: fh
  - action: mqtt.publish
    data:
      topic: transit-display/hourly
      retain: true
      payload: >-
        {% set ns = namespace(h=[]) %}
        {% for e in fh['weather.home'].forecast[:8] %}
          {% set ns.h = ns.h + [{'t': (as_datetime(e.datetime) | as_local).strftime('%-I%p') | lower,
                                 'c': e.condition, 'f': e.temperature | round | int,
                                 'p': e.precipitation_probability | default(0) | int}] %}
        {% endfor %}
        {{ {'h': ns.h} | to_json }}
mode: restart
```

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
  LAN: `mqtt: connected`, `weather: ...`, `notify: N item(s)`, `pause: on`, and
  a `draw: ...` line for every panel refresh.
- To see what the broker is holding for the display, call `mqtt.dump` with
  topic `transit-display/#`. HA writes the result to `mqtt_dump.txt` in its
  config directory.
