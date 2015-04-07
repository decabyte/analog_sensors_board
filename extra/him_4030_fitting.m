% hih-4030 fitting at 2.56V or 5.0V analog reference

clear all; close all; clc;

% model parameter
%ADC_V = 2.56 / 1023;
ADC_V = 5.0 / 1023;

% input model data
%   V@0%    V@100%  T
A = [
    0.8     4.07    0;
    0.8     3.90    25;
    0.8     3.50    85 
];

% analog values
X = round(A(:,[1 2]) ./ ADC_V);

% RH values
Y = zeros(size(X));
Y(:,2) = 100 * ones(size(X,1),1);

% data fitting
P = zeros(size(X,1), 2);

for n = 1 : size(X,1)
    P(n,:) = polyfit(X(n,:), [0 100], 1);
    
    fprintf('y = (%2.3f) x + (%2.3f) \n', P(n,:));
end

% linear regression over T
M = polyfit(A(:,3), P(:,1), 1);
B = polyfit(A(:,3), P(:,2), 1);

fprintf('\nRH%% = (%1.5f * temp + %1.5f) * x + (%1.5f * temp + %1.5f) \n\n', ... 
    M(1), M(2), B(1), B(2));

%% data plot
f = figure();
p = plot(X(:), Y(:), 'or'); grid on; hold on;

ylim([0 100]);
vh = line([1023 1023], [0 100]);
set(vh,'LineStyle','--','Color','k');

for k = 1 : size(A,1)
    x = 100:1600;
    t = A(k,3);
    y = (M(1)*t + M(2)) * x + (B(1)*t + B(2));
    
    p = plot(x,y,'b--'); 
    fprintf('temp = %2.1f\n', t);
end

x = 320:1600;
t = randi([30 50], 1);
y = (M(1)*t + M(2)) * x + (B(1)*t + B(2));

p = plot(x,y,'r'); ylim([0 100]);
fprintf('temp = %2.1f\n', t);

