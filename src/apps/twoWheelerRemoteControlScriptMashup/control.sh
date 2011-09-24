#! /bin/sh

echo "FirstBot control program:"
echo "Use numeric keypad for controlling the robot or q to quit."

./setup-motors.sh

while true
do
  read -s -n1 x

  case "$x" in
  '8')
     ./forward.sh
     ;;
  '2')
     ./back.sh
     ;;
  '4')
     ./left.sh
     ;;
  '6')
     ./right.sh
     ;;
  '5')
     ./stop.sh
     ;;
  '7')
     ./forwardleft.sh
     ;;
  '9')
     ./forwardright.sh
     ;;
  '1')
     ./backleft.sh
     ;;
  '3')
     ./backright.sh
     ;;
  'q')
     echo "Bye!"
     exit 0
     ;;
   esac
done

