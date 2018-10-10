myid  =999999;

function set_myid(x)
 myid =x; 
end

function LoadMonsterData()
my_type = math.random(1,4)

if(my_type == 1) then
	my_x = math.random(0,150)
	my_y = math.random(0,150)
	my_level = 5;
	my_hp = 100;
	my_name = "Goblin"
elseif(my_type == 2) then
	my_x = math.random(150,299)
	my_y = math.random(0,150)
	my_level = 8;
	my_hp = 150;
	my_name = "Orc"
elseif(my_type == 3) then
	my_x = math.random(0,150)
	my_y = math.random(150,299)
	my_level = 15;
	my_hp = 250;
	my_name = "Oger"
elseif(my_type == 4) then
	my_x = math.random(150,299)
	my_y = math.random(150,299)
	my_level = 20;
	my_hp = 500;
	my_name = "DarkKnight"
	end
	return my_type, my_x, my_y, my_level, my_hp, my_name
end