# NuoDB PHP PDO Native Driver #

[![Build Status](https://api.travis-ci.org/nuodb/nuodb-php-pdo.png?branch=master)](http://travis-ci.org/nuodb/nuodb-php-pdo)

## Installing Necessary Dependencies ##

In order to build the driver you must have PHP plus developer tools installed.
This may be done from source or using package managers.

PHP version 5.3 or 5.4 is required. 

NuoDB installed


## Build & Install on Unix ##

```bash
phpize --clean
phpize
configure --with-pdo-nuodb=/opt/nuodb
make clean
make
sudo make install
```


## Build on Windows ##


```cmd
phpize --clean
phpize
configure --with-pdo-nuodb="C:\Program Files\NuoDB"
nmake clean
nmake
```

## Windows Install Example with PHP installed in C:\php ##

```cmd
php -i | findstr extension_dir
del /S /Q C:\php\php_pdo_nuodb.*
copy Release_TS\php_pdo_nuodb.* C:\php
```



## RUNNING TESTS ##

Prerequisites for running the unit tests include having a running database at test@localhost.  The privileged credentials must be the username/password: "dba/dba".  The non-privileged credentials must be username/password: "cloud/user". The database can be started using these commands on Unix:

```bash
java -jar /opt/nuodb/jar/nuoagent.jar --broker --domain test --password bird --bin-dir /opt/nuodb/bin &
nuodbmgr --broker localhost --password bird --command "start process sm host localhost database test archive /tmp/nuodb_test_data waitForRunning true initialize true"
nuodbmgr --broker localhost --password bird --command "start process te host localhost database test options '--dba-user dba --dba-password dba'"
echo "create user cloud password 'user';" | /opt/nuodb/bin/nuosql test@localhost --user dba --password dba
```

Run the following commands to run the tests:

```bash
pear run-tests tests/*.phpt
```

## LOGGING ##

You can optionally enable and control logging with the following PHP configuration variables:

  pdo_nuodb.enable_log
  pdo_nuodb.log_level
  pdo_nuodb.logfile_path

pdo_nuodb.enable_log defaults to zero (0).  To enable logging, set pdo_nuodb.enable_log=1.

pdo_nuodb.log_level defaults to one (1).  You can use levels 1-5. The higher level numbers have more detail.  The higher level numbers include lesser levels:

  1 - errors/exceptions only
  2 - SQL statements
  3 - API
  4 - Functions
  5 - Everything

pdo_nuodb.logfile_path defaults to /tmp/nuodb_pdo.log.  You can override that default by specifying your own path.



[![githalytics.com alpha](https://cruel-carlota.pagodabox.com/eebba2b3f495d19d760a0b42e0ce67fd "githalytics.com")](http://githalytics.com/nuodb/nuodb-php-pdo)
