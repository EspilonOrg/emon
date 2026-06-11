Output demo/demo-multiport.gif
Set Shell "bash"
Set FontSize 14
Set Width 1100
Set Height 600
Set Theme "Dracula"
Set Padding 20
Set PlaybackSpeed 1.0
Set TypingSpeed 50ms

Hide
Type "source ~/git/espilon/emon/demo/shell_setup.sh"
Enter
Type "cd ~/git/espilon/emon"
Enter
Type "clear && cat demo/lain.txt"
Enter
Sleep 500ms
Show

Sleep 2500ms

Type "emon --verbose --name /dev/ttyUSB0=ESP32 --name /dev/ttyACM0=Arduino /dev/ttyUSB0 /dev/ttyACM0"
Sleep 500ms
Enter
Sleep 10000ms

Ctrl+C
Sleep 500ms
