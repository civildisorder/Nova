#!/bin/bash

QUERY1="
PRAGMA journal_mode = WAL;

CREATE TABLE suspect_alerts (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	suspect VARCHAR(40),
	timestamp TIMESTAMP,
	statistics INT,
	classification DOUBLE
);

CREATE TABLE statistics (
		id INTEGER PRIMARY KEY AUTOINCREMENT,

		ip_traffic_distribution DOUBLE,
		port_traffic_distribution DOUBLE,
		packet_size_mean DOUBLE,
		packet_size_deviation DOUBLE,
		distinct_ips DOUBLE,
		distinct_tcp_ports DOUBLE,
		distinct_udp_ports DOUBLE,
		avg_tcp_ports_per_host DOUBLE,
		avg_udp_ports_per_host DOUBLE,
		tcp_percent_syn DOUBLE,
		tcp_percent_fin DOUBLE,
		tcp_percent_rst DOUBLE,
		tcp_percent_synack DOUBLE,
		haystack_percent_contacted DOUBLE
);"

novadDbFilePath="$DESTDIR/usr/share/nova/userFiles/data/novadDatabase.db"
rm -fr "$novadDbFilePath"
sqlite3 "$novadDbFilePath" <<< $QUERY1
chgrp nova "$novadDbFilePath"
chmod g+rw "$novadDbFilePath"



QUERY2="
PRAGMA journal_mode = WAL;

CREATE TABLE firstrun(
	run TIMESTAMP PRIMARY KEY
);

CREATE TABLE credentials(
	user VARCHAR(100),
	pass VARCHAR(100)
);"


quasarDbFilePath="$DESTDIR/usr/share/nova/userFiles/data/quasarDatabase.db"
rm -fr "$quasarDbFilePath"
sqlite3 "$quasarDbFilePath" <<< $QUERY2
chgrp nova "$quasarDbFilePath"
chmod g+rw "$quasarDbFilePath"

echo "SQL schema has been set up for nova."
