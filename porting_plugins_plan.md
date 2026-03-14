
Search the equivalent plugins in ~/ws_plotjuggler/src/PlotJuggler/plotjuggler_plugins/

Original plugins may not be consistent, but works correctly, new ones must have a consistent design pattern, if possible
(not a strict requirement, but a recomendation).

Try to use a good separation of concerns between GUI and core logic,
adding unit tests to the latter, when possible.

# DataLoadCSV

Including all the unit tests. additionally, have a look at jsoncons/1.5.0 that seems to support csv too

# DataLoadMCAP

you can use conan package mcap/2.1.1

# DataLoadULog

refer to DataLoadULog2

can fetch this library: https://github.com/PX4/ulog_cpp/releases/tag/v1.0.1

# DataLoadParquet

~/ws_plotjuggler/src/PlotJuggler/plotjuggler_plugins/DataLoadParquet

We already depend on Arrow through conan

Also take a critcial look at the ipleentation and deterine if we need a btter UI / business logic separation

# DataStreamFoxgloveBridge and DataStreamPlotJugglerBridge

You will need to qdd the Qt websocket library. 

# DataStreamMQTT

Have a critical look at it and decide (do a web search) if we should use either

paho-mqtt-cpp/1.5.3  or mosquitto/1.6.12 (conan dependencies)

# DataStreamZMQ
	
Conan cppzmq/4.11.0

Also take a deeper look at the correctness of the plugin, this hasn't been maintained in a while

# ParserROS

Consider if we should fetch https://github.com/facontidavide/rosx_introspection

compared with the vendored version to decide if any change is needed

# ParserJSON

not a plugin in the original plotjuggler. Compare jsoncons/1.5.0 and nlohmann_json/3.12.0

Read plotjuggler_app/nlohmann_parsers.h	

# ParserProtobuf

Use protobuf/6.33.5 

Take a critical look at the current implementation
