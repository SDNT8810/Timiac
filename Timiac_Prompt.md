like /octopus project, i want to develop the /Timiac project
so make files and directories that are needed acordingly in Timiac directory

here is the project description :

i will use two octopos board , each for controlling 6 motors (as we have 12 arm)
i will use two ESP-12S (one on each octopos board) to handle the comunication between boards

one ESP board must be the hotspot (server side) and one be client side
also i must be able to connect to the server module with my phone to see the controll panel (like the control panel of Lumiac project (ESP_RF_Octopus) --> check /Timiac/Simulator/index.html)

two ESP modules must sent required gcodes to their moderboards (octopos boards)

i must be able to sent commands and g-codes through the web application (with my phone that is connected to the timiac wifi server (first ESP board))

also i like to be able to control arms one by one manually lite the Lumiac project



--------------------------------------------------------------------

some considerations:
when more than one phone or other clients connect to the server or when some disconect and reconect , it must works fine and stable ! handle that

when one esp board is facing issue, and lost the connection, it must handle the reconections safely so user don't neel unstable connection ! it most reconnect automaticaly and sync the status and continiue working

sometimes octopos boards face some problmes , overal controler program must be robust , so when one octopos is in truble, it must understand and tell me in web app logs , also they must stay sync and i don't like to see 6 arm is moving while 6 other arms are stoped due to issue in one board ! 
you understand my worrying ? i want stable and fully synced solution between two ESP and Octopos boards




--------------------------------------------------------------------


now im going to start the timiac project with 2 motor only 
lets make 3 esp codes :
one for octopus board 1
one for octopus board 2
one as master
master esp must make hotspot (wifi server) with user : Timiac , password: 12345678
the other two esp must be connected to that server
also prepaire the marlin firmware so i can have one firmware to upload on two microSD and put in the octopus boards
each esp-12s must send commands to the attached octopus board to controll only X axis of it
so the master esp must send one axis command to each of two esp's
you get what i am doing ? right now i have only 2 motor , each connected to X axis of one of octopus boards 
but later i  will have 12 motors , 6 connected to each octopus .
name them X, Y, Z, A, B, C 
so master must name them X1 , Y1, Z1, A1, B1, C1, X2, Y2, Z2, A2, B2, C2 
so input files (gcodes) to master network will be something like this:
G90 X1 10 X2 35 F350

--------------------------------------------------------------------
we still have that time out issue. when all arms are at max strock and i hit homeing key, it will take more than 60 sec to home them , so something hits a timeout or watch dog , and i have to restart the hole system to be able to do the homeing again ! 
to resolve it, do two things:
1- increase homing speed 50 percent
2- increase the timeouts (make it 180 sec if you find where it is !)

also i like it be more robust ! so when something hapens, i don't like to have access phisicaly to the boards and restart them or cut the power and power it again to restart the whole system ! i like to have some retry mechanisms so the boards try to reconect to the master when something went wrong or due to lost connection!
also add boards status in dashboards (both release and dev) so i can see which one is not ready and functioning! 
so we might need a "Fix" key to resolve issues remotely (if we can) instead of hardware power cut !
now when i press "Stop" key, i have no other way to start the system again !
there must be a Fix of Proceed key after Stop or any connectivity issue

after you did this things, i need to add a new feature to the project:
i like to have a new section in dashboard, so i can write Wifi name and password of my wifi modem or phone hotspot , so the master esp try to connect to that wifi and have access to the internet while its still having the server on and esp1 and 2 remains connected to the master

later i will add more features if i have internet access !


--------------------------------------------------------------------

when master is connected to the internet, we must be able to read the time from the wifi (local time)

also i like to be able to have access to the dashboard from other locations when it is connected to the internet. so lets make it public !
i will give it required cloudflare and gcloud cridentials so we can publish it on :
timiac.it domain , and only registered admin can sign in with gmail and have access to that dashboard

--------------------------------------------------------------------

its not fixed yet, only X Y Z 1 and 2 are homeing and then it stops and make strange noises


i like to have state of micro switches on dev dashboard
so i must have 12 small red lamps , when i triger them with hand it must show me which one is trigered (make it green)


--------------------------------------------------------------------

clock arm map is like this :

X1 ---> 12 o'clock
Y1 ---> 11 o'clock
Z1 ---> 10 o'clock
A1 ----> 9 o'clock
B1 ----> 8 o'clock
C1 ----> 7 o'clock
X2 ----> 6 o'clock
Y2 ----> 5 o'clock
Z2 ----> 4 o'clock
A2 ----> 3 o'clock
B2 ----> 2 o'clock
C2 ----> 1 o'clock

so please update the logic and g-codes accordingly

--------------------------------------------------------------------

ah it was a wiring issue that i fixed and worked fine
now lets make the motors less noisy !
when i start the project and take controll , its silent , but after i home the arms . they are making a continioues noise (like a fan) to hold the possition. 
movement is fine , but when they dont move, they are making a nois !

how can we resolve it ?


also i like to have the sensoreless homing option , so i can home all arms even without end stop micro switches , becouse am going to remove them later ! 
just prepair it so i can test it in master dashboard

--------------------------------------------------------------------

now lets have two version for timiac project:
one for developer mode dashboard (as it is)
and one for release mode : running clock without debugings features

so i can upload the firmwares with :
python run.py octopus_dev --sd I:\
python run.py esp1_dev --port COM5
python run.py esp2_dev --port COM5
python run.py master_dev --port COM5

for developer mode

and with :
python run.py octopus_release --sd I:\
python run.py esp1_release --port COM5
python run.py esp2_release --port COM5
python run.py master_release --port COM5

for release version

but befor that, make sure sensorless homeing is not used ! i didn't liked that !

also please make sure all gcodes and movements are handled in master ESP32 not in microSD cart. so i can remove the SDcart after i uploaded the octopus firmware (like the way we handled it in Lumiac project)




G90 X1 1 Y1 1 Z1 1 A1 1 B1 1 C1 1 X2 1 Y2 1 Z2 1 A2 1 B2 1 C2 1 F180
G90 X1 30 Y1 30 Z1 30 A1 30 B1 30 C1 30 X2 30 Y2 30 Z2 30 A2 30 B2 30 C2 30 F300






proceed

also the strugle please 
it must run smoothly , not batch of movements !

please check the retrive of homing , how much is it ?
if its 2 mm , make it 5 , if its 5 , make it 7

lets have "Next 5 min" , "Next hour", and "Struggle" keys in release dashboard too

we dont need "Open" key anymore , its same as home , 
Close must send all arms to 52

also make the max strok 52 instead of 48



-------------------------------------------------------------

https://timiac.it/ is assigned to timiac simulator and its running on another pc (vps)
i like to have access to clock (real hardware) with https://timiac.control.it/ domain when master_esp is connected to wifi (has internet access) so i can see the dashboard on https://timiac.control.it/

what parameters do you need to set cloudflare ? 
i think i have provided some gcloud and cloudflare tokens, let me know if you need any other things

also it must have sign-in with gmail ! so only registered emails can have access
here is the list of gmails that must have access:
davood.space@gmail.com
davoudtanha91@gmail.com
davoudnikkhouy@gmail.com

let the required cridentials be pushed to github , so i can just pull this repo on another pc and run it there without needing copy/past cridentials ! i don't mind !

-------------------------------------------------------------

do some small modifications:

1- reduce "next hour" animation speed 30 percent

2- change "next hour" animation like this : all arms full open, then all arms close, except the onse that are showing current time. so dont close them all then show time ! close them while its shoing time (to reduce one step and decrease overal animation time)
right now its like this: all open, all close , then open arms that are showing time ! merge that 2 last steps

3- strugle is too short. make it do that movement two time. and make it more ! for example if its moving only 20 mm per arm, make it 40 mm. and do that two time without pause ! like a smooth strugle

4- update simulation so i can see this new changes in simulation too


-------------------------------------------------------------

i want to change the dictance sensor. current sensore is measuring distance with sonar sensor (sound). but there are some mechanical parts in front of sensor, so it can't capture the hand gusture. i need a narrower sensor becouse there is not enouth open space in front of the clock.

can we adjust the sensore wide of veiw ? i think we cant ! i checked!
so let's use a lida beam
i want to use this one:
Heschen Interruttore di prossimità fotoelettrico a infrarossi a colonna M18, tipo di riflessione diffusa, E3F-DS30B2, distanza di rilevamento 10-30CM, 10-30VDC 200mA, NPN normalmente chiuso, 3 fili https://amzn.eu/d/0bsadsaG https://amzn.eu/d/0bsadsaG

i have two tasks for you:
1- give me new diagram for master board: all wires that we connect to master-esp board
2- update the codes acourdingly
