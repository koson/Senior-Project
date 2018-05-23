%% set accuracy plot
% need to import the setacc csv

figure(1); clf;

% high = stdH./setpoint;
% low = stdL./setpoint;
% hlim = setpoint + 10;
% llim = setpoint - 10;
% setzoom = setpoint(20:30)
% errorbar(setpoint,setpoint,high,low)
% hold on
% plot(setpoint, hlim, setpoint, llim)
% legend('Measured Temperature with range', 'Upper Contract Limit',' Lower Contract Limit')
% 
% axes('Position',[.7 .7 .2 .2])
% box on
% errorbar(setzoom,setzoom,high(20:30),low(20:30))
% hold on
% plot(setzoom, hlim(20:30), setzoom, llim(20:30))

%% Temp accuracy Plot

%errorbar(act,meas,err)


%% VDC Volatge measurement
% import newfile4 from oscope usb

% l=4.75;
% h=5.25;
% plot(Time1, XCH2)
% hold on
% line([-0.03, 0.03],[h,h])
% hold on
% line([-0.03, 0.03],[l,l])

%% triac

plot(Time, XCH1, Time1+0.0042, XCH2)




