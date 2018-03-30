% script is meant to be run after create_channel has applied channel effect
% and filtered data has been run through dmoed_chan_filtered_iq

clear all;
close all;

data_dir = './output2';
create_channel_mat = [data_dir,filesep,'gen_data.mat'];
estimated_channel_bin = [data_dir,filesep,'channel_G.bin'];

create_channel_dat = load(create_channel_mat);
chan = create_channel_dat.chan;
rx_iq_data = create_channel_dat.rx_iq_data;
tx_iq_data = create_channel_dat.tx_iq_data;

freqs = linspace(-1,1,length(rx_iq_data));

figure;
plot(freqs,10*log10(abs(fftshift(fft(chan)))),'linewidth',2);
xlabel('Frequency (MHz)','fontsize',15);
ylabel('Amplitude (dB)','fontsize',15);
title('Actual Channel Frequency Response','fontsize',15);
ax1 = gca;
f1 = gcf;
set(ax1,'FontSize',15);
saveas(gcf,[data_dir,filesep,'full_actual_chan.png']);

figure;
plot(freqs,10*log10(abs(fftshift(fft(rx_iq_data)))),'linewidth',2);
xlabel('Frequency (MHz)','fontsize',15);
ylabel('Amplitude (dB)','fontsize',15);
title('Channel-Filtered Signal','fontsize',15);
ax2  = gca;
f2 = gcf;
set(ax2,'FontSize',15);
saveas(gcf,[data_dir,filesep,'full_rx_spec.png']);

figure;
plot(freqs,10*log10(abs(fftshift(fft(tx_iq_data)))),'linewidth',2);
xlabel('Frequency (MHz)','fontsize',15);
ylabel('Amplitude (dB)','fontsize',15);
title('Tx''d Signal','fontsize',15);
set(gca,'FontSize',15);
saveas(gcf,[data_dir,filesep,'full_tx_spec.png']);

linkaxes([ax1,ax2],'x');
set(ax1,'XLim',[-.4,.4]);
set(ax2,'XLim',[-.4,.4]);
saveas(f1,[data_dir,filesep,'zoomed_actual_chan.png']);
saveas(f2,[data_dir,filesep,'zoomed_rx_spec.png']);

estimated_channel_dat = parse_iq_data(estimated_channel_bin);
estimated_channel_dat(1) = (estimated_channel_dat(1)+estimated_channel_dat(end))/2;
estimated_channel_dat = estimated_channel_dat(estimated_channel_dat~=0);

figure;
freqs = linspace(-.4,.4,length(estimated_channel_dat));
plot(freqs,fftshift((abs(estimated_channel_dat))),'linewidth',2);
xlabel('Frequency (MHz)','fontsize',15);
ylabel('Amplitude','fontsize',15);
title('Estimated Channel Frequency Response','fontsize',15);
set(gca,'FontSize',15);
saveas(gcf,[data_dir,filesep,'estimated_channel.png']);

