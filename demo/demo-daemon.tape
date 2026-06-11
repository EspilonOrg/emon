Output demo/demo-daemon.gif
Set Shell "bash"
Set FontSize 14
Set Width 1100
Set Height 620
Set Theme "Dracula"
Set Padding 20
Set PlaybackSpeed 1.0
Set TypingSpeed 60ms

Hide
Type "source ~/git/espilon/emon/demo/shell_setup.sh"
Enter
Type "cd ~/git/espilon/emon"
Enter
Type "clear && cat demo/lain.txt"
Enter
Sleep 500ms
Type "rm -rf /tmp/emon_demo_logs && mkdir -p /tmp/emon_demo_logs"
Enter
Sleep 200ms
Show

Sleep 2500ms

Type "emon --bg --verbose --logdir /tmp/emon_demo_logs /dev/ttyUSB0"
Sleep 500ms
Enter
Sleep 1500ms

Type "emon status --logdir /tmp/emon_demo_logs"
Sleep 300ms
Enter
Sleep 1000ms

Sleep 2500ms

Type "emon stop --logdir /tmp/emon_demo_logs"
Sleep 300ms
Enter
Sleep 1000ms

Type "emon status --logdir /tmp/emon_demo_logs"
Sleep 300ms
Enter
Sleep 800ms

Type "cat /tmp/emon_demo_logs/ttyUSB0.log"
Sleep 300ms
Enter
Sleep 3000ms
