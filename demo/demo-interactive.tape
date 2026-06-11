Output demo/demo-interactive.gif
Set Shell "bash"
Set FontSize 14
Set Width 1100
Set Height 600
Set Theme "Dracula"
Set Padding 20
Set PlaybackSpeed 1.0
Set TypingSpeed 100ms

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

Type "emon -i --verbose -p tests/firmwares/arduino_interactive/arduino_interactive.pat /dev/ttyACM0"
Sleep 500ms
Enter
Sleep 4000ms

Type "ping"
Enter
Sleep 2500ms

Type "oom"
Enter
Sleep 2500ms

Type "crash"
Enter
Sleep 5000ms

Ctrl+C
Sleep 1000ms
