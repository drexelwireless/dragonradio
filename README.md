# full-radio

Cloning this repository
  - git clone https://github.com/dwsl/full-radio.git
  - make the dependencies
  - run make in the cloned repo to get started
  - (to update when changes are made to master run "git pull origin master" in your full-radio folder)

Running in loopback (you don't need a USRP)
  - set main.cc's loopback bool to true
  - run make at the terminal
  - run ./full-radio at the terminal
  - open up a new terminal and run ping 10.10.10.2
  - the loopback mode will "spoof" the data packet to make it appear like it's source was 10.10.10.2 even though it was transmitted from 10.10.10.1. This makes the operating system think it's got data to handle, so it does. Therefore, all types of network applications will be supported even in loopback.
  
Running in normal mode (loopback set to false)
  - get atleast two nodes connected to USRPs
  - set one node's node_id to 2 in main.cc (other should be left at 1)
  - run make at the terminal of both nodes
  - run ./full-radio at the terminal of both nodes
  - open up new terminal on both nodes
  - run some kind of network application (ping,scp,nc,MGEN,iperf) node 1 will have ip address 10.10.10.1 and 2 will have 10.10.10.2
  
  
