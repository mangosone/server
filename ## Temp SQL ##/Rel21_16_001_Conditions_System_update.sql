-- ----------------------------------------------------------------
-- This is an attempt to create a full transactional MaNGOS update
-- Now compatible with newer MySql Databases (v1.5)
-- ----------------------------------------------------------------
DROP PROCEDURE IF EXISTS `update_mangos`; 

DELIMITER $$

CREATE DEFINER=`root`@`localhost` PROCEDURE `update_mangos`()
BEGIN
    DECLARE bRollback BOOL  DEFAULT FALSE ;
    DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET `bRollback` = TRUE;

    -- Current Values (TODO - must be a better way to do this)
    SET @cCurVersion := (SELECT `version` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
    SET @cCurStructure := (SELECT `structure` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
    SET @cCurContent := (SELECT `content` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);

    -- Expected Values
    SET @cOldVersion = '21'; 
    SET @cOldStructure = '15'; 
    SET @cOldContent = '053';

    -- New Values
    SET @cNewVersion = '21';
    SET @cNewStructure = '16';
    SET @cNewContent = '001';
                            -- DESCRIPTION IS 30 Characters MAX    
    SET @cNewDescription = 'Conditions System Update';

                        -- COMMENT is 150 Characters MAX
    SET @cNewComment = 'Conditions System Update';

    -- Evaluate all settings
    SET @cCurResult := (SELECT `description` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
    SET @cOldResult := (SELECT `description` FROM `db_version` WHERE `version`=@cOldVersion AND `structure`=@cOldStructure AND `content`=@cOldContent);
    SET @cNewResult := (SELECT `description` FROM `db_version` WHERE `version`=@cNewVersion AND `structure`=@cNewStructure AND `content`=@cNewContent);

    IF (@cCurResult = @cOldResult) THEN    -- Does the current version match the expected version
        -- APPLY UPDATE
        START TRANSACTION;
        -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
        -- -- PLACE UPDATE SQL BELOW -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
        -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -

    DROP TABLE IF EXISTS `conditions`;

    CREATE TABLE `conditions` (
      `condition_entry` mediumint(8) unsigned NOT NULL AUTO_INCREMENT COMMENT 'Identifier',
      `type` tinyint(3) NOT NULL DEFAULT '0' COMMENT 'Type of the condition.',
      `value1` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'Data field One for the condition.',
      `value2` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'Data field Two for the condition.',
      `comments` varchar(200) DEFAULT NULL,
      PRIMARY KEY (`condition_entry`),
      UNIQUE KEY `unique_conditions` (`type`,`value1`,`value2`)
    ) ENGINE=MyISAM AUTO_INCREMENT=31 DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='Condition System';


insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1,2,16309,1,'ID:2848 - Item Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (2,2,30622,1,'ID:4150,4151,4152 - Heroic Key2 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (3,2,30623,1,'ID:4363,4364,4365 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (4,2,30633,1,'ID:4404,4405,4406,4407 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (5,2,30634,1,'ID:4467,4468,4469 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (6,2,30635,1,'ID:4320,4321 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (7,2,30637,1,'ID:4150,4151,4152 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (8,8,7487,1,'ID:3528,3529 - Completed Quest');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (9,8,7761,1,'ID:3726 - Completed Quest');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (10,8,10285,1,'ID:4320 - Completed Quest');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (11,15,1,1,'ID:4386 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (12,15,8,1,'ID:2230 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (13,15,10,1,'ID:78,145,228 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (14,15,15,1,'ID:101 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (15,15,17,1,'ID:244 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (16,15,19,1,'ID:257 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (17,15,20,1,'ID:45,324,523,606,610,612,614 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (18,15,25,1,'ID:442 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (19,15,30,1,'ID:286,446,902,924,3133,3134 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (20,15,40,1,'ID:1466 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (21,15,45,1,'ID:1468,2214,2216,2217,2567,3183,3184,3185,3186,3187,3189,3728 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (22,15,50,1,'ID:2848,2886,2890,3528,3529,3928,4008,4010 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (23,15,51,1,'ID:4055,4156 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (24,15,55,1,'ID:4150,4151,4152,4363,4364,4365,4404,4405,4406 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (25,15,58,1,'ID:4352,4354 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (26,15,60,1,'ID:3726 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (27,15,65,1,'ID:4153,4407,4535 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (28,15,66,1,'ID:4320,4321 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (29,15,68,1,'ID:4131,4135,4467,4468,4469,4470,4738 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (30,15,70,1,'ID:4311,4312,4313,4319,4416,4598,4887,4889 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (31,-1,2,7,'ID:4150 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (32,-1,2,24,'ID:4151 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (33,-1,1,22,'ID:2848 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (34,-1,8,22,'ID:3528,3529 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (36,-1,9,26,'ID:3726 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (37,-1,31,24,'ID:4150 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (38,-1,32,7,'ID:4151 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (39,-1,7,24,'ID:4152 COMBO 1');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (40,-1,39,2,'ID:4152 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (42,-1,6,10,'ID:4320 COMBO 1');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (43,-1,42,28,'ID:4320 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (44,-1,6,28,'ID:4321 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (45,-1,3,24,'ID:4363,4364,4365 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (48,-1,4,24,'ID:4405,4406 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (50,-1,4,27,'ID:4407 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (51,-1,5,29,'ID:4467,4468,4469 COMBO');

DROP TABLE IF EXISTS `areatrigger_teleport`;

CREATE TABLE `areatrigger_teleport` (
  `id` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'The ID of the trigger (See AreaTrigger.dbc).',
  `name` text COMMENT 'The name of the teleport areatrigger.',
  `target_map` smallint(5) unsigned NOT NULL DEFAULT '0' COMMENT 'The destination map id. (See map.dbc)',
  `target_position_x` float NOT NULL DEFAULT '0' COMMENT 'The x location of the player at the destination.',
  `target_position_y` float NOT NULL DEFAULT '0' COMMENT 'The y location of the player at the destination.',
  `target_position_z` float NOT NULL DEFAULT '0' COMMENT 'The z location of the player at the destination.',
  `target_orientation` float NOT NULL DEFAULT '0' COMMENT 'The orientation of the player at the destination.',
  `condition_id` mediumint(8) NOT NULL DEFAULT '0' COMMENT 'The Condition_id reference',
  PRIMARY KEY (`id`),
  FULLTEXT KEY `name` (`name`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='Trigger System';

insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (45,'Scarlet Monastery - Graveyard (Entrance)',189,1688.99,1053.48,18.6775,0.00117,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (78,'DeadMines Entrance',36,-16.4,-383.07,61.78,1.86,13);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (101,'Stormwind Stockades Entrance',34,54.23,0.28,-18.34,6.26,14);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (107,'Stormwind Vault Entrance',35,-0.91,40.57,-24.23,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (109,'Stormwind Vault Instance',0,-8653.45,606.19,91.16,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (119,'DeadMines Instance Start',0,-11208.3,1672.52,24.66,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (121,'Deadmines Instance End',0,-11339.4,1571.16,100.56,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (145,'Shadowfang Keep Entrance',33,-229.135,2109.18,76.8898,1.267,13);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (194,'Shadowfang keep - Entrance',0,-232.796,1568.28,76.8909,4.398,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (226,'The Barrens - Wailing Caverns',1,-740.059,-2214.23,16.1374,5.68,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (228,'The Barrens - Wailing Caverns',43,-163.49,132.9,-73.66,5.83,13);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (242,'Razorfen Kraul Instance Start',1,-4464.92,-1666.24,81.9,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (244,'Razorfen Kraul Entrance',47,1943,1544.63,82,1.38,15);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (257,'Blackphantom Deeps Entrance',48,-151.89,106.96,-39.87,4.53,16);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (259,'Blackfathom Deeps Instance Start',1,4247.74,745.879,-24.5299,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (286,'Uldaman Entrance',70,-226.8,49.09,-46.03,1.39,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (288,'Uldaman Instance Start',0,-6066.73,-2955.63,209.776,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (322,'Gnomeregan Instance Start',0,-5163.33,927.623,257.188,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (324,'Gnomeregan Entrance',90,-332.22,-2.28,-150.86,2.77,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (442,'Razorfen Downs Entrance',129,2592.55,1107.5,51.29,4.74,18);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (444,'Razorfen Downs Instance Start',1,-4658.12,-2526.35,82.9671,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (446,'Altar of Atal\'Hakkar Entrance',109,-319.24,99.9,-131.85,3.19,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (448,'Altar Of Atal\'Hakkar Instance Start',0,-10175.1,-3995.15,-112.9,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (503,'Stockades Instance',0,-8764.83,846.075,87.4842,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (523,'Gnomeregan Train Depot Entrance',90,-736.51,2.71,-249.99,3.14,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (525,'Gnomeregan Train Depot Instance',0,-4858.27,756.435,244.923,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (527,'Teddrassil - Ruth Theran',1,8786.36,967.445,30.197,3.39632,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (542,'Teddrassil - Darnassus',1,9945.13,2616.89,1316.46,4.61446,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (602,'Scarlet Monastery - Graveyard (Exit)',0,2913.92,-802.404,160.333,3.50405,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (604,'Scarlet Monastery - Cathedral (Exit)',0,2906.14,-813.772,160.333,1.95739,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (606,'Scarlet Monastery - Armory (Exit)',0,2884.45,-822.01,160.333,1.95268,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (608,'Scarlet Monastery - Library (Exit)',0,2870.9,-820.164,160.333,0.387856,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (610,'Scarlet Monastery - Cathedral (Entrance)',189,855.683,1321.5,18.6709,0.001747,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (612,'Scarlet Monastery - Armory (Entrance)',189,1610.83,-323.433,18.6738,6.28022,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (614,'Scarlet Monastery - Library (Entrance)',189,255.346,-209.09,18.6773,6.26656,17);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (702,'Stormwind - Wizard Sanctum Room',0,-9015.8,874.6,148.617,5.31,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (704,'Stormwind - Wizard Sanctum Tower Portal',0,-9017.4,886.3,29.6206,5.38,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (882,'Uldaman Instance End',0,-6620.48,-3765.19,266.91,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (902,'Uldaman Exit',70,-214.02,383.607,-38.7687,0.5,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (922,'Zul\'Farrak Instance Start',1,-6796.49,-2890.77,8.88063,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (924,'Zul\'Farrak Entrance',209,1213.52,841.59,8.93,6.09,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (943,'Leap of Faith - End of fall',1,-5187.47,-2804.32,-8.375,5.76,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1064,'Onyxia\'s Lair - Dustwallow Instance',1,-4747.17,-3753.27,49.8122,0.713271,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1466,'Blackrock Mountain - Searing Gorge Instance?',230,458.32,26.52,-70.67,4.95,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1468,'Blackrock Spire - Searing Gorge Instance (Inside)',229,78.5083,-225.044,49.839,5.1,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1470,'Blackrock Spire - Searing Gorge Instance',0,-7524.19,-1230.13,285.743,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1472,'Blackrock Dephts - Searing Gorge Instance',0,-7179.63,-923.667,166.416,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2166,'Deeprun Tram - Ironforge Instance (Inside)',0,-4838.95,-1318.46,501.868,1.42372,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2171,'Deeprun Tram - Stormwind Instance (Inside)',0,-8364.57,535.981,91.7969,2.24619,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2173,'Deeprun Tram - Stormwind Instance',369,68.3006,2490.91,-4.29647,3.12192,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2175,'Deeprun Tram - Ironforge Instance',369,69.2542,10.257,-4.29664,3.09832,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2214,'Stratholme - Eastern Plaguelands Instance',329,3593.15,-3646.56,138.5,5.33,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2216,'Stratholme - Eastern Plaguelands Instance',329,3395.09,-3380.25,142.702,0.1,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2217,'Stratholme - Eastern Plaguelands Instance',329,3395.09,-3380.25,142.702,0.1,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2221,'Stratholme - Eastern Plaguelands Instance (Inside)',0,3235.46,-4050.6,110.01,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2226,'Ragefire Chasm - Ogrimmar Instance (Inside)',1,1813.49,-4418.58,-18.57,1.78,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2230,'Ragefire Chasm - Ogrimmar Instance',389,3.81,-14.82,-17.84,4.39,12);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2527,'Hall of Legends - Ogrimmar',450,221.322,74.4933,25.7195,0.484836,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2530,'Hall of Legends - Ogrimmar (Inside)',1,1637.32,-4242.7,56.1827,4.1927,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2532,'Stormwind - Champions Hall',449,-0.299116,4.39156,-0.255884,1.54805,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2534,'Stormwind (Inside) - Champions Hall',0,-8762.45,403.062,103.902,5.34463,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2567,'Scholomance Entrance',289,196.37,127.05,134.91,6.09,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2568,'Scholomance Instance',0,1275.05,-2552.03,90.3994,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2606,'Alterac Valley - Horde Exit',0,534.868,-1087.68,106.119,3.35758,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2608,'Alterac Valley - Alliance Exit',0,98.432,-182.274,127.52,5.02654,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2848,'Onyxia\'s Lair Entrance',249,29.1607,-71.3372,-8.18032,4.58,33);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2886,'The Molten Bridge',409,1096,-467,-104.6,3.64,22);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2890,'Molten Core Entrance, Inside',230,1115.35,-457.35,-102.7,0.5,22);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3126,'Maraudon',1,-1186.98,2875.95,85.7258,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3131,'Maraudon',1,-1471.07,2618.57,76.1944,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3133,'Maraudon',349,1019.69,-458.31,-43.43,0.31,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3134,'Maraudon',349,752.91,-616.53,-33.11,1.37,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3183,'Dire Maul',429,44.4499,-154.822,-2.71201,0,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3184,'Dire Maul',429,-201.11,-328.66,-2.72,5.22,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3185,'Dire Maul',429,9.31119,-837.085,-32.5305,0,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3186,'Dire Maul',429,-62.9658,159.867,-3.46206,0,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3187,'Dire Maul',429,31.5609,159.45,-3.4777,0.01,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3189,'Dire Maul',429,255.249,-16.0561,-2.58737,4.7,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3190,'Dire Maul',1,-3831.79,1250.23,160.223,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3191,'Dire Maul',1,-3747.96,1249.18,160.217,3.15827,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3193,'Dire Maul',1,-3520.65,1077.72,161.138,1.5009,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3194,'Dire Maul',1,-3737.48,934.975,160.973,3.13864,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3195,'Dire Maul',1,-3980.58,776.193,161.006,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3196,'Dire Maul',1,-4030.21,127.966,26.8109,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3197,'Dire Maul',1,-3577.67,841.859,134.594,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3528,'The Molten Core Window Entrance',409,1096,-467,-104.6,3.64,34);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3529,'The Molten Core Window(Lava) Entrance',409,1096,-467,-104.6,3.64,34);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3726,'Blackwing Lair - Blackrock Mountain - Eastern Kingdoms',469,-7673.03,-1106.08,396.651,0.703353,36);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3728,'Blackrock Spire, Unknown',229,174.74,-474.77,116.84,3.2,20);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3928,'Zul\'Gurub Entrance ',309,-11916.1,-1230.53,92.5334,0,22);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3930,'Zul\'Gurub Exit ',0,-11916.3,-1208.37,92.2868,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3948,'Arathi Basin Alliance Out',0,-1198,-2533,22,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3949,'Arathi Basin Horde Out',0,-817,-3509,73,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4006,'Ruins Of Ahn\'Qiraj (Inside)',1,-8418.5,1505.94,31.8232,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4008,'Ruins Of Ahn\'Qiraj (Outside)',509,-8429.74,1512.14,31.9074,0,22);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4010,'Ahn\'Qiraj Temple (Outside)',531,-8231.33,2010.6,129.861,0,22);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4012,'Ahn\'Qiraj Temple (Inside)',1,-8242.67,1992.06,129.072,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4055,'Naxxramas (Exit)',533,3005.87,-3435.01,293.882,0,23);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4131,'Karazhan, Main (Entrance)',532,-11100,-2003.98,49.8927,0.577268,29);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4135,'Karazhan, Service (Entrance)',532,-11040.1,-1996.85,94.6837,2.20224,29);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4145,'The Shattered Halls (Exit)',530,-311.16,3082.1,-3.71,4.92,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4147,'The Blood Furnace (Exit)',530,-303.506,3164.82,31.7425,5.24025,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4149,'Magtheridon\'s Lair (Exit)',530,-313.679,3088.35,-116.502,2.05307,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4150,'Hellfire Ramparts (Entrance)',543,-1355.24,1641.12,68.2491,0.6687,37);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4151,'The Shattered Halls (Entrance)',540,-40.8716,-19.7538,-13.8065,1.11133,38);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4152,'The Blood Furnace (Entrance)',542,-3.9967,14.6363,-44.8009,4.88748,40);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4153,'Magtheridon\'s Lair (Entrance)',544,187.843,35.9232,67.9252,4.79879,27);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4156,'Naxxramas (Entrance)',533,3498.28,-5349.9,144.968,1.31324,23);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4297,'Hellfire Ramparts (Exit)',530,-360.671,3071.9,-15.0977,5.14274,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4311,'Battle Of Mount Hyjal, Alliance Base (Entrance)',534,4954,-1886.2,1326,0.13,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4312,'Battle Of Mount Hyjal, Horde Base (Entrance)',534,5497.33,-2971.14,1537.63,2.832,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4313,'Battle Of Mount Hyjal, Night Elf Base (Entrance)',534,5152.33,-3364.4,1644.74,6.2,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4319,'Caverns Of Time, Battle Of Mount Hyjal (Entrance) ',534,4259.61,-4233.77,868.199,2.53,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4320,'Caverns Of Time, Black Morass (Entrance)',269,-1496.24,7034.7,32.5619,1.75699,43);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4321,'Caverns Of Time, Old Hillsbrad Foothills (Entrance)',560,2741.87,1315.25,14.0423,2.96016,44);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4322,'Caverns Of Time, Black Morass (Exit)',1,-8765.66,-4175,-209.863,5.53463,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4323,'Caverns Of Time, Battle Of Mount Hyjal (Exit)',1,-8177.5,-4183,-168,4.5,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4324,'Caverns Of Time, Old Hillsbrad Foothills (Exit)',1,-8334.98,-4055.32,-207.737,3.27431,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4352,'Outland To Dark Portal',0,-11877.7,-3204.49,-18.49,0.23,25);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4354,'Dark Portal To Outland',530,-248,956,85,0,25);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4363,'The Underbog (Entrance)',546,9.71391,-16.2008,-2.75334,5.57082,45);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4364,'The Steamvault (Entrance)',545,-13.8425,6.7542,-4.2586,0,45);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4365,'The Slave Pens (Entrance)',547,120.101,-131.957,-0.801547,1.47574,45);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4366,'The Steamvault (Exit)',530,816.59,6934.67,-80.5446,4.53174,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4367,'The Underbog (Exit)',530,777.089,6763.45,-72.0662,2.72453,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4379,'The Slave Pens (Exit)',530,719.508,6999.34,-73.0743,4.52702,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4386,'Sunstrider Isle to Eastern Plaguelands',0,3476.36,-4493.36,137.49,0,11);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4397,'Shadow Labyrinth (Exit)',530,-3645.06,4943.62,-101.048,6.27058,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4399,'Auchenai Crypts (Exit)',530,-3361.96,4660.41,-101.048,4.76654,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4401,'Mana Tombs (Exit)',530,-3079.81,4943.04,-101.047,3.16432,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4403,'Sethekk Halls (Exit)',530,-3362.22,5225.77,-101.049,1.62101,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4404,'Auchenai Crypts (Entrance)',558,-21.8975,0.16,-0.1206,0.0353412,4);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4405,'Mana Tombs (Entrance)',557,0.0191,0.9478,-0.9543,3.03164,48);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4406,'Sethekk Halls (Entrance)',556,-4.6811,-0.0930796,0.0062,0.0353424,48);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4407,'Shadow Labyrinth (Entrance)',555,0.488033,-0.215935,-1.12788,3.15888,50);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4409,'Eastern Plaguelands To Sunstrider Isle',530,6123,-7005,138,5,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4416,'Serpentshrine Cavern (Entrance)',548,2.5343,-0.022318,821.727,0.004512,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4418,'Serpentshrine Cavern (Exit)',530,827.011,6865.47,-63.7844,3.06507,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4436,'Karazhan, Main (Exit)',0,-11112.9,-2005.89,49.3307,4.02516,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4455,'The Mechanar (Exit)',530,3312.09,1331.89,505.559,2.00554,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4457,'The Eye (Exit)',530,3087.31,1373.79,184.643,1.52918,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4459,'The Botanica (Exit)',530,3413.65,1483.32,182.838,2.54432,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4461,'The Arcatraz (Exit)',530,2862.41,1546.09,252.161,0.805457,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4467,'The Botanica (Entrance)',553,40.0395,-28.613,-1.1189,2.35856,51);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4468,'The Arcatraz (Entrance)',552,-1.23165,0.0143459,-0.204293,0.0157123,51);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4469,'The Mechanar (Entrance)',554,-28.906,0.680314,-1.81282,0.0345509,51);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4470,'The Eye (Entrance)',550,-10.8021,-1.15045,-2.42833,6.22821,29);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4487,'Battle Of Mount Hyjal (Exit)',1,-8177.5,-4183,-168,4.5,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4523,'Socrethar\'s Seat To Mainland',530,4773.76,3451.27,105.15,3.84,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4534,'Gruul\'s Lair (Exit)',530,3549.8,5085.97,2.46332,2.25742,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4535,'Gruul\'s Lair (Entrance)',565,62.7842,35.462,-3.9835,1.41844,27);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4561,'Invasion Point, Cataclysm (Return Point)',530,-3278.63,2831.31,123.01,1.56,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4562,'Invasion Point, Cataclysm (Return Point)',530,-3278.63,2831.31,123.01,1.56,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4598,'Black Temple (Entrance)',564,96.4462,1002.35,-86.9984,6.15675,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4619,'Black Temple (Exit)',530,-3653.51,317.493,36.1671,6.24941,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4738,'Zul\'Aman (Entrance)',568,120.7,1776,43.46,4.7713,29);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4739,'Zul\'Aman (Exit)',530,6851.5,-7997.68,192.36,1.56688,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4885,'Magisters\' Terrace (Exit)',530,12884.6,-7336.17,65.48,1.09,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4887,'Magisters\' Terrace (Entrance)',585,7.09,-0.45,-2.8,0.05,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4889,'Sunwell Plateau (Entrance)',580,1790.65,925.67,15.15,3.1,30);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4891,'Sunwell Plateau (Exit)',530,12560.8,-6774.59,15.08,6.25,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4612,'The Botanica',530,3407.11,1488.48,182.838,2.50112,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4614,'The Mechanar',530,2869.89,1552.76,252.159,0.733993,0);


        -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
        -- -- PLACE UPDATE SQL ABOVE -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
        -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -

        -- If we get here ok, commit the changes
        IF bRollback = TRUE THEN
            ROLLBACK;
            SHOW ERRORS;
            SELECT '* UPDATE FAILED *' AS `===== Status =====`,@cCurResult AS `===== DB is on Version: =====`;
        ELSE
            COMMIT;
            -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
            -- UPDATE THE DB VERSION
            -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
            INSERT INTO `db_version` VALUES (@cNewVersion, @cNewStructure, @cNewContent, @cNewDescription, @cNewComment);
            SET @cNewResult := (SELECT `description` FROM `db_version` WHERE `version`=@cNewVersion AND `structure`=@cNewStructure AND `content`=@cNewContent);

            SELECT '* UPDATE COMPLETE *' AS `===== Status =====`,@cNewResult AS `===== DB is now on Version =====`;
        END IF;
    ELSE    -- Current version is not the expected version
        IF (@cCurResult = @cNewResult) THEN    -- Does the current version match the new version
            SELECT '* UPDATE SKIPPED *' AS `===== Status =====`,@cCurResult AS `===== DB is already on Version =====`;
        ELSE    -- Current version is not one related to this update
            IF(@cCurResult IS NULL) THEN    -- Something has gone wrong
                SELECT '* UPDATE FAILED *' AS `===== Status =====`,'Unable to locate DB Version Information' AS `============= Error Message =============`;
            ELSE
                IF(@cOldResult IS NULL) THEN    -- Something has gone wrong
                    SET @cCurVersion := (SELECT `version` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
                    SET @cCurStructure := (SELECT `STRUCTURE` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
                    SET @cCurContent := (SELECT `Content` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
                    SET @cCurOutput = CONCAT(@cCurVersion, '_', @cCurStructure, '_', @cCurContent, ' - ',@cCurResult);
                    SET @cOldResult = CONCAT('Rel',@cOldVersion, '_', @cOldStructure, '_', @cOldContent, ' - ','IS NOT APPLIED');
                    SELECT '* UPDATE SKIPPED *' AS `===== Status =====`,@cOldResult AS `=== Expected ===`,@cCurOutput AS `===== Found Version =====`;
                ELSE
                    SET @cCurVersion := (SELECT `version` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
                    SET @cCurStructure := (SELECT `STRUCTURE` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
                    SET @cCurContent := (SELECT `Content` FROM `db_version` ORDER BY `version` DESC, `STRUCTURE` DESC, `CONTENT` DESC LIMIT 0,1);
                    SET @cCurOutput = CONCAT(@cCurVersion, '_', @cCurStructure, '_', @cCurContent, ' - ',@cCurResult);
                    SELECT '* UPDATE SKIPPED *' AS `===== Status =====`,@cOldResult AS `=== Expected ===`,@cCurOutput AS `===== Found Version =====`;
                END IF;
            END IF;
        END IF;
    END IF;
END $$

DELIMITER ;

-- Execute the procedure
CALL update_mangos();

-- Drop the procedure
DROP PROCEDURE IF EXISTS `update_mangos`;


