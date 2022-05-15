Info: 
	Proxy-server listens client on TCP-port 8888.
Make:
	bash make

Usage: 
	sudo ./proxy_postgres_x [Debug flag] [SERVER_PORT]
		Parameters are optional.
		Debug flag = 0 (Off, by default) or 1 (On),
		SERVER_PORT - default 5432

SQL Queries will be logged in file queries.txt.

Folder test_run:
	Example scripts for parallel workload testing using sysbench.

Example of sysbench parallel test on 30 clients.

SQL statistics:
    queries performed:
        read:                            104048
        write:                           29649
        other:                           14894
        total:                           148591
    transactions:                        7411   (24.64 per sec.)
    queries:                             148591 (494.07 per sec.)
    ignored errors:                      21     (0.07 per sec.)
    reconnects:                          0      (0.00 per sec.)

General statistics:
    total time:                          300.7452s
    total number of events:              7411

Latency (ms):
         min:                                  765.81
         avg:                                 1216.12
         max:                                 4400.42
         95th percentile:                     1506.29
         sum:                              9012683.15

Threads fairness:
    events (avg/stddev):           247.0333/10.61
    execution time (avg/stddev):   300.4228/0.28
