Output demo/demo-basic.gif
Set Shell "bash"
Set FontSize 14
Set Width 1100
Set Height 700
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
Show

Sleep 3500ms

Type "emon --verbose /dev/ttyUSB0"
Sleep 500ms
Enter
Sleep 9000ms

Ctrl+C
Sleep 500ms
