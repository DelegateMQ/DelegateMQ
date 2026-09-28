-- cellutron_topics.lua
-- Remote ID labels for the DelegateMQ Wireshark dissector (tools/wireshark/dmq.lua).
-- Copy next to dmq.lua in the Wireshark plugins folder. See tools/wireshark/README.md.
--
-- Source: common/util/RemoteConfig.h (RID_* values) and the Send/Receive calls in
-- each node's system/System.cpp. A remote ID is a channel, not a topic: the
-- controller sends five sensor topics on RID_SENSOR_STATUS, and the packet
-- doesn't say which one, so that entry lists them all.
--
-- UDP ports: GUI 5010, Controller 5011, Safety 5013 (UDP heuristic finds them
-- without configuration).

dmq_topics = dmq_topics or {}

dmq_topics[100] = "cell/cmd/run"                  -- RID_START_PROCESS
dmq_topics[101] = "cell/cmd/centrifuge_speed"     -- RID_CENTRIFUGE_SPEED
dmq_topics[102] = "hw/sensor/rpm"                 -- RID_CENTRIFUGE_STATUS
dmq_topics[103] = "cell/status/run"               -- RID_RUN_STATUS
dmq_topics[104] = "cell/cmd/abort"                -- RID_STOP_PROCESS
dmq_topics[105] = "cell/fault"                    -- RID_FAULT_EVENT
dmq_topics[106] = "hw/status/actuator"            -- RID_ACTUATOR_STATUS
dmq_topics[107] = "hw/status/sensor, hw/sensor/{air,pressure}_{inlet,outlet}"  -- RID_SENSOR_STATUS
dmq_topics[108] = "sys/heartbeat/safety"          -- RID_SAFETY_HB
dmq_topics[109] = "sys/heartbeat/controller"      -- RID_CONTROLLER_HB
dmq_topics[110] = "sys/heartbeat/gui"             -- RID_GUI_HB
