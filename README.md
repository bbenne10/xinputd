# Xinputd - simple Xorg input device watcher

Xinputd is a simple daemon that executes a command when an Xorg input device is added or removed.
By default, Xinputd forks to background and exits when the X server exits.

## USAGE: 

xinputd [option] command_with_options_to_execute

## OPTIONS: 

* -h  Show help and exit
* -n  Don't fork to background

## EXAMPLE

To swap caps and escape every time you plug a new device in:

```
  $ xinputd setxkb -option caps:escape
```

More complex solutions may be better solved with a script as the execution target.

## TODO

* More fine grained control about what devices trigger command execution (Right now all devices do!)
* Debouncing execution so we don't run more often than necessary (Devices that take a second to power up will currently trigger a new execution)

## HERITAGE AND THANKS

This project owes quite a bit to portix for [srandrd](https://bitbucket.com/portix/srandrd).
Without his work there, I would never have considered this a viable approach to take to solve this issue.
Additionally, I cribbed quite a bit of code from that project. Thanks, portix!

In addition, I owe a beer to the author of almost every poster of an xcb code snippet on the internet. So cheers!
