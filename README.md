# qredisclient

[API documenation](http://uglide.github.io/qredisclient/docs/html/annotated.html) |
[Examples](https://github.com/uglide/qredisclient/tree/master/examples)


[![Build Status](https://travis-ci.org/uglide/qredisclient.svg)](https://travis-ci.org/uglide/qredisclient)
[![Build status](https://ci.appveyor.com/api/projects/status/2qtv3nsj16kkm621?svg=true)](https://ci.appveyor.com/project/uglide/qredisclient)
[![Coverage Status](https://coveralls.io/repos/github/uglide/qredisclient/badge.svg?branch=master)](https://coveralls.io/github/uglide/qredisclient?branch=master)

Asynchronous Qt-based Redis client with SSL and SSH tunnelling support. This client is developed for [RedisDesktopManager](https://github.com/uglide/RedisDesktopManager) by [Igor Malinovskiy](https://github.com/uglide)

```c++
#include "qredisclient/redisclient.h"

int main(int argc, char *argv[])
{
  // Init qredisclient
  initRedisClient();
  
  // Create connection to local redis
  RedisClient::ConnectionConfig config("127.0.0.1");
  RedisClient::Connection connection(config);
  
  // Run command and wait for result
  connection.commandSync("PING"); 
  
  // Run command in async mode
  connection.command("PING");
  
  // Run command in db #2
  connection.command("PING", 2); 
  
  // Run async command with callback
  connection.command("PING", [](RedisClient::Response r) { 
    QVariant val = r.getValue(); // get value from response
    // do stuff
  });

  // See more usage examples in examples folder
}

```

***Supported Qt versions:*** 5.4-5.9
