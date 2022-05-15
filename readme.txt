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
