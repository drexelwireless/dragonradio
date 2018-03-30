% script applies rician channel to tx data produced by full-radio
% channel-filtered data gets saved off and should be demodulated with
% full-radio demod stand-alone application (demod_chan_filtered_iq)

clear all;
close all;

tx_iq_file = './txdata6/txed_data_4.bin';
output_dir  = './output2';

tx_iq_data = parse_iq_data(tx_iq_file);
tx_iq_data = tx_iq_data(:);

H = comm.RicianChannel('SampleRate',2e6,'KFactor',3.0,...
                       'MaximumDopplerShift',0.1,...
                       'PathDelays',[0.0,0.5,1.0,1.5]*1e-6,...
                       'AveragePathGains',[0,-10,-2,-6],...
                       'PathGainsOutputPort',true,...
                       'RandomStream','mt19937ar with seed',...
                       'Seed',444);
                   
rx_iq_data = H(tx_iq_data);

figure;
plot(10*log10(abs(fftshift(fft(tx_iq_data)))));

figure;
plot(10*log10(abs(fftshift(fft(rx_iq_data)))));
ax1 = gca;

chan = ifft(fft(rx_iq_data)./fft(tx_iq_data));

% [nn,xx] = hist((abs(rx_iq_data)),15);
% 
% figure;
% scatter(xx,nn,40,'MarkerFaceColor','r');
% %axis([-100,0,0,max(nn)]);

figure;
plot(10*log10(abs(fftshift(fft(chan)))));
%axis([0,length(chan),-40,20]);
ax2 = gca;

linkaxes([ax1,ax2],'x');

mkdir(output_dir);
save([output_dir,filesep,'gen_data.mat'],'chan','rx_iq_data','tx_iq_data','H');

rx_iq_data = rx_iq_data.';
tmp = [real(rx_iq_data);imag(rx_iq_data)];
tmp = tmp(:);
f = fopen([output_dir,filesep,'channel_sim_output.bin'],'w');
fwrite(f,tmp,'float');
fclose(f);

