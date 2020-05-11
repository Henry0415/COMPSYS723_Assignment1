# COMPSYS723_Assignment1
This repo is for assignment 1 (frequency analyser project) of CS723.

To run this project:
1. Create a quartus project
2. Programme the FPGA with the .sof file
3. Create an empty Nios II project in the Eclipse editor
4. Import the freertos_test.c file 
5. Build the BSP first
6. Build the application project
7. Right click the application project and select 'run as' then choose 'nios II hardware'

To change the Frequency and ROC thresholds:
1. Using the PS/2 keyboard,press 'f' or 'r' to select frequency or ROC respectively
2. Type the value you want to set as the threshold value (integer value only)
3. Press the Enter Key