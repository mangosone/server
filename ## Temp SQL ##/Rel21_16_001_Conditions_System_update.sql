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

truncate table `conditions`;
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1,9,11668,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (2,2,11511,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (3,7,197,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (4,7,165,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (5,7,164,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (6,7,755,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (7,7,186,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (8,7,333,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (9,7,202,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (10,6,469,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (11,9,1846,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (12,6,67,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (13,2,13370,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (14,10,0,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (15,7,171,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (16,1,33377,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (17,8,8460,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (18,2,12384,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (19,9,5056,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (20,8,8464,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (21,8,4242,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (22,8,5128,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (23,8,5251,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (24,8,5384,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (25,9,11515,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (26,8,6383,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (27,12,2,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (28,8,7786,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (29,9,11541,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (30,9,10855,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (31,9,10821,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (32,9,9923,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (33,9,9924,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (34,8,10970,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (35,9,9955,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (36,9,10852,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (37,9,10641,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (38,8,10265,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (39,9,10565,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (40,9,11020,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (41,9,10804,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (42,8,10682,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (43,9,10980,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (44,8,11004,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (45,8,11014,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (46,9,8620,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (47,2,12766,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (48,2,12765,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (49,2,19727,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (50,8,11075,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (239,5,942,5,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (240,5,942,4,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (392,5,942,7,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (435,5,942,6,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1733,38,181054,12,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1734,8,9152,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (871,9,9164,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (872,2,22628,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (873,-2,871,872,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1737,9,10814,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1735,9,10146,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1736,9,10340,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1058,1,31973,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1027,8,9305,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1028,8,9312,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1029,-1,1027,1028,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1030,8,9280,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1031,8,9369,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1032,-2,1031,1030,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1033,8,9294,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1034,8,9314,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1035,8,9452,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1038,8,9506,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1043,16,23759,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1044,22,9514,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1045,-1,22,1038,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1046,-1,1043,1044,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1047,-1,1045,1046,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (855,1,23768,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (856,1,23769,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (857,1,23767,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (858,1,23738,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (859,1,23766,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (860,1,23737,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (861,1,23736,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (862,1,23735,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (863,-2,855,856,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (864,-2,857,858,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (865,-2,859,860,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (866,-2,861,862,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (867,-2,863,864,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (868,-2,865,866,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (869,-2,867,868,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (870,-3,869,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1738,9,10201,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1739,1,32756,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (504,9,10722,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (505,1,38157,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (625,-1,505,504,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (117,8,9991,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (383,9,10646,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (693,-3,117,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (162,9,10172,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (511,9,10044,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (164,26,324,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (165,11,24755,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (521,-1,165,164,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (719,33,1,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (720,33,3,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (721,33,10,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (722,33,22,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (723,33,28,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1048,9,9595,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1092,9,2118,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1093,24,7586,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1094,-1,1092,1093,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (114,8,975,0,'');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (229,24,12384,1,'');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (724,-1,229,114,'completed quest 975 and not have item 12384');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (804,9,5247,0,'Used in gossip_menu_option 4743');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (805,8,5245,0,'Used in gossip_menu | text_id 4743 | 5817');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1740,9,10368,0,'Control gossip option for quest.');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1741,9,30,0,'Gossip quest check');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1742,9,272,0,'Gossip quest check');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1743,-2,1741,1742,'Druid gossip quest check');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1744,14,0,1024,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1745,-1,10,1744,'Alliance gossip Druid flight');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1746,-1,12,1744,'Horde gossip Druid flight');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1747,-2,1744,93,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1748,9,9067,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (92,14,0,2,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1749,-1,1744,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (965,8,3785,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (79,14,0,128,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (523,-1,79,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (544,-1,74,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (332,24,17126,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (333,19,6881,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (567,-1,333,332,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (530,-1,73,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (93,14,0,64,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (80,14,0,1279,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (814,5,54,7,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (823,14,1037,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (833,-1,814,823,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (841,14,64,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (853,-2,833,841,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (73,14,0,8,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (72,14,0,1527,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (74,14,0,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (157,9,3909,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (107,16,9279,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (108,2,9279,1,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (75,14,0,511,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (77,14,0,16,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (169,15,10,1,'ID:78,145,228 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (566,-1,77,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (82,14,0,4,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (542,-1,82,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (103,14,0,256,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (543,-1,103,169,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1750,24,13544,1,'Gossip menu 3310 second check.');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1751,-1,24,1750,'Replace Spectral Essence Gossip menu 3310 check');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1752,9,9437,0,NULL);
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1753,8,4512,0,'Show gossip text 3099 if quest, A Little Slime Goes a Long Way (Part 1), is rewarded');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1754,8,4513,0,'Show gossip text 3098 if quest, A Little Slime Goes a Long Way (Part 2), is rewarded');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1755,-3,1754,0,'Only show if quest, A Little Slime Goes a Long Way (Part 2), is NOT rewarded');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1756,-1,1753,1755,'Condition Check for A Little Slime Goes a Long Way gossip');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1101,2,16309,1,'ID:2848 - Item Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1102,2,30622,1,'ID:4150,4151,4152 - Heroic Key2 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1103,2,30623,1,'ID:4363,4364,4365 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1104,2,30633,1,'ID:4404,4405,4406,4407 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1105,2,30634,1,'ID:4467,4468,4469 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1106,2,30635,1,'ID:4320,4321 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1107,2,30637,1,'ID:4150,4151,4152 - Heroic Key1 Required');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1108,8,7487,0,'ID:3528,3529 - Completed Quest');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1109,8,7761,0,'ID:3726 - Completed Quest');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1110,8,10285,0,'ID:4320 - Completed Quest');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1111,15,1,1,'ID:4386 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1112,15,8,1,'ID:2230 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1114,15,15,1,'ID:101 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1115,15,17,1,'ID:244 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1116,15,19,1,'ID:257 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1117,15,20,1,'ID:45,324,523,606,610,612,614 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1118,15,25,1,'ID:442 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1119,15,30,1,'ID:286,446,902,924,3133,3134 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1120,15,40,1,'ID:1466 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1121,15,45,1,'ID:1468,2214,2216,2217,2567,3183,3184,3185,3186,3187,3189,3728 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1122,15,50,1,'ID:2848,2886,2890,3528,3529,3928,4008,4010 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1123,15,51,1,'ID:4055,4156 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1124,15,55,1,'ID:4150,4151,4152,4363,4364,4365,4404,4405,4406 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1125,15,58,1,'ID:4352,4354 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1126,15,60,1,'ID:3726 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1150,15,65,1,'ID:4153,4407,4535 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1128,15,66,1,'ID:4320,4321 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1129,15,68,1,'ID:4131,4135,4467,4468,4469,4470,4738 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1130,15,70,1,'ID:4311,4312,4313,4319,4416,4598,4887,4889 - Required Level');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1131,-1,1102,1107,'ID:4150 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1132,-1,1102,1124,'ID:4151 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1133,-1,1101,1122,'ID:2848 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1134,-1,1108,1122,'ID:3528,3529 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1136,-1,1109,1126,'ID:3726 Combo');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1137,-1,1131,1124,'ID:4150 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1138,-1,1132,1107,'ID:4151 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1139,-1,1107,1124,'ID:4152 COMBO 1');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1140,-1,1139,1102,'ID:4152 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1142,-1,1106,1110,'ID:4320 COMBO 1');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1143,-1,1142,1128,'ID:4320 COMBO 2');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1144,-1,1106,1128,'ID:4321 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1145,-1,1103,1124,'ID:4363,4364,4365 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1148,-1,1104,1124,'ID:4405,4406 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (2117,-1,1104,1150,'ID:4407 COMBO');
insert  into `conditions`(`condition_entry`,`type`,`value1`,`value2`,`comments`) values (1151,-1,1105,1129,'ID:4467,4468,4469 COMBO');

DROP TABLE IF EXISTS `areatrigger_teleport`;

CREATE TABLE `areatrigger_teleport` (
  `id` MEDIUMINT(8) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'The ID of the trigger (See AreaTrigger.dbc).',
  `name` TEXT COMMENT 'The name of the teleport areatrigger.',
  `target_map` SMALLINT(5) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'The destination map id. (See map.dbc)',
  `target_position_x` FLOAT NOT NULL DEFAULT '0' COMMENT 'The x location of the player at the destination.',
  `target_position_y` FLOAT NOT NULL DEFAULT '0' COMMENT 'The y location of the player at the destination.',
  `target_position_z` FLOAT NOT NULL DEFAULT '0' COMMENT 'The z location of the player at the destination.',
  `target_orientation` FLOAT NOT NULL DEFAULT '0' COMMENT 'The orientation of the player at the destination.',
  `condition_id` MEDIUMINT(8) NOT NULL DEFAULT '0' COMMENT 'The Condition_id reference',
  PRIMARY KEY (`id`),
  FULLTEXT KEY `name` (`name`)
) ENGINE=MYISAM DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='Trigger System';

insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (45,'Scarlet Monastery - Graveyard (Entrance)',189,1688.99,1053.48,18.6775,0.00117,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (78,'DeadMines Entrance',36,-16.4,-383.07,61.78,1.86,169);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (101,'Stormwind Stockades Entrance',34,54.23,0.28,-18.34,6.26,1114);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (107,'Stormwind Vault Entrance',35,-0.91,40.57,-24.23,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (109,'Stormwind Vault Instance',0,-8653.45,606.19,91.16,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (119,'DeadMines Instance Start',0,-11208.3,1672.52,24.66,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (121,'Deadmines Instance End',0,-11339.4,1571.16,100.56,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (145,'Shadowfang Keep Entrance',33,-229.135,2109.18,76.8898,1.267,169);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (194,'Shadowfang keep - Entrance',0,-232.796,1568.28,76.8909,4.398,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (226,'The Barrens - Wailing Caverns',1,-740.059,-2214.23,16.1374,5.68,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (228,'The Barrens - Wailing Caverns',43,-163.49,132.9,-73.66,5.83,169);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (242,'Razorfen Kraul Instance Start',1,-4464.92,-1666.24,81.9,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (244,'Razorfen Kraul Entrance',47,1943,1544.63,82,1.38,1115);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (257,'Blackphantom Deeps Entrance',48,-151.89,106.96,-39.87,4.53,1116);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (259,'Blackfathom Deeps Instance Start',1,4247.74,745.879,-24.5299,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (286,'Uldaman Entrance',70,-226.8,49.09,-46.03,1.39,1119);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (288,'Uldaman Instance Start',0,-6066.73,-2955.63,209.776,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (322,'Gnomeregan Instance Start',0,-5163.33,927.623,257.188,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (324,'Gnomeregan Entrance',90,-332.22,-2.28,-150.86,2.77,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (442,'Razorfen Downs Entrance',129,2592.55,1107.5,51.29,4.74,1118);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (444,'Razorfen Downs Instance Start',1,-4658.12,-2526.35,82.9671,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (446,'Altar of Atal\'Hakkar Entrance',109,-319.24,99.9,-131.85,3.19,1119);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (448,'Altar Of Atal\'Hakkar Instance Start',0,-10175.1,-3995.15,-112.9,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (503,'Stockades Instance',0,-8764.83,846.075,87.4842,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (523,'Gnomeregan Train Depot Entrance',90,-736.51,2.71,-249.99,3.14,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (525,'Gnomeregan Train Depot Instance',0,-4858.27,756.435,244.923,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (527,'Teddrassil - Ruth Theran',1,8786.36,967.445,30.197,3.39632,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (542,'Teddrassil - Darnassus',1,9945.13,2616.89,1316.46,4.61446,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (602,'Scarlet Monastery - Graveyard (Exit)',0,2913.92,-802.404,160.333,3.50405,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (604,'Scarlet Monastery - Cathedral (Exit)',0,2906.14,-813.772,160.333,1.95739,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (606,'Scarlet Monastery - Armory (Exit)',0,2884.45,-822.01,160.333,1.95268,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (608,'Scarlet Monastery - Library (Exit)',0,2870.9,-820.164,160.333,0.387856,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (610,'Scarlet Monastery - Cathedral (Entrance)',189,855.683,1321.5,18.6709,0.001747,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (612,'Scarlet Monastery - Armory (Entrance)',189,1610.83,-323.433,18.6738,6.28022,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (614,'Scarlet Monastery - Library (Entrance)',189,255.346,-209.09,18.6773,6.26656,1117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (702,'Stormwind - Wizard Sanctum Room',0,-9015.8,874.6,148.617,5.31,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (704,'Stormwind - Wizard Sanctum Tower Portal',0,-9017.4,886.3,29.6206,5.38,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (882,'Uldaman Instance End',0,-6620.48,-3765.19,266.91,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (902,'Uldaman Exit',70,-214.02,383.607,-38.7687,0.5,1119);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (922,'Zul\'Farrak Instance Start',1,-6796.49,-2890.77,8.88063,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (924,'Zul\'Farrak Entrance',209,1213.52,841.59,8.93,6.09,19);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (943,'Leap of Faith - End of fall',1,-5187.47,-2804.32,-8.375,5.76,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1064,'Onyxia\'s Lair - Dustwallow Instance',1,-4747.17,-3753.27,49.8122,0.713271,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1466,'Blackrock Mountain - Searing Gorge Instance?',230,458.32,26.52,-70.67,4.95,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1468,'Blackrock Spire - Searing Gorge Instance (Inside)',229,78.5083,-225.044,49.839,5.1,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1470,'Blackrock Spire - Searing Gorge Instance',0,-7524.19,-1230.13,285.743,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (1472,'Blackrock Dephts - Searing Gorge Instance',0,-7179.63,-923.667,166.416,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2166,'Deeprun Tram - Ironforge Instance (Inside)',0,-4838.95,-1318.46,501.868,1.42372,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2171,'Deeprun Tram - Stormwind Instance (Inside)',0,-8364.57,535.981,91.7969,2.24619,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2173,'Deeprun Tram - Stormwind Instance',369,68.3006,2490.91,-4.29647,3.12192,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2175,'Deeprun Tram - Ironforge Instance',369,69.2542,10.257,-4.29664,3.09832,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2214,'Stratholme - Eastern Plaguelands Instance',329,3593.15,-3646.56,138.5,5.33,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2216,'Stratholme - Eastern Plaguelands Instance',329,3395.09,-3380.25,142.702,0.1,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2217,'Stratholme - Eastern Plaguelands Instance',329,3395.09,-3380.25,142.702,0.1,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2221,'Stratholme - Eastern Plaguelands Instance (Inside)',0,3235.46,-4050.6,110.01,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2226,'Ragefire Chasm - Ogrimmar Instance (Inside)',1,1813.49,-4418.58,-18.57,1.78,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2230,'Ragefire Chasm - Ogrimmar Instance',389,3.81,-14.82,-17.84,4.39,1112);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2527,'Hall of Legends - Ogrimmar',450,221.322,74.4933,25.7195,0.484836,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2530,'Hall of Legends - Ogrimmar (Inside)',1,1637.32,-4242.7,56.1827,4.1927,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2532,'Stormwind - Champions Hall',449,-0.299116,4.39156,-0.255884,1.54805,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2534,'Stormwind (Inside) - Champions Hall',0,-8762.45,403.062,103.902,5.34463,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2567,'Scholomance Entrance',289,196.37,127.05,134.91,6.09,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2568,'Scholomance Instance',0,1275.05,-2552.03,90.3994,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2606,'Alterac Valley - Horde Exit',0,534.868,-1087.68,106.119,3.35758,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2608,'Alterac Valley - Alliance Exit',0,98.432,-182.274,127.52,5.02654,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2848,'Onyxia\'s Lair Entrance',249,29.1607,-71.3372,-8.18032,4.58,1133);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2886,'The Molten Bridge',409,1096,-467,-104.6,3.64,1122);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (2890,'Molten Core Entrance, Inside',230,1115.35,-457.35,-102.7,0.5,1122);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3126,'Maraudon',1,-1186.98,2875.95,85.7258,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3131,'Maraudon',1,-1471.07,2618.57,76.1944,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3133,'Maraudon',349,1019.69,-458.31,-43.43,0.31,1119);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3134,'Maraudon',349,752.91,-616.53,-33.11,1.37,1119);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3183,'Dire Maul',429,44.4499,-154.822,-2.71201,0,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3184,'Dire Maul',429,-201.11,-328.66,-2.72,5.22,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3185,'Dire Maul',429,9.31119,-837.085,-32.5305,0,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3186,'Dire Maul',429,-62.9658,159.867,-3.46206,0,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3187,'Dire Maul',429,31.5609,159.45,-3.4777,0.01,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3189,'Dire Maul',429,255.249,-16.0561,-2.58737,4.7,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3190,'Dire Maul',1,-3831.79,1250.23,160.223,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3191,'Dire Maul',1,-3747.96,1249.18,160.217,3.15827,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3193,'Dire Maul',1,-3520.65,1077.72,161.138,1.5009,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3194,'Dire Maul',1,-3737.48,934.975,160.973,3.13864,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3195,'Dire Maul',1,-3980.58,776.193,161.006,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3196,'Dire Maul',1,-4030.21,127.966,26.8109,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3197,'Dire Maul',1,-3577.67,841.859,134.594,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3528,'The Molten Core Window Entrance',409,1096,-467,-104.6,3.64,1134);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3529,'The Molten Core Window(Lava) Entrance',409,1096,-467,-104.6,3.64,1134);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3726,'Blackwing Lair - Blackrock Mountain - Eastern Kingdoms',469,-7673.03,-1106.08,396.651,0.703353,1136);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3728,'Blackrock Spire, Unknown',229,174.74,-474.77,116.84,3.2,1120);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3928,'Zul\'Gurub Entrance ',309,-11916.1,-1230.53,92.5334,0,1122);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3930,'Zul\'Gurub Exit ',0,-11916.3,-1208.37,92.2868,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3948,'Arathi Basin Alliance Out',0,-1198,-2533,22,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (3949,'Arathi Basin Horde Out',0,-817,-3509,73,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4006,'Ruins Of Ahn\'Qiraj (Inside)',1,-8418.5,1505.94,31.8232,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4008,'Ruins Of Ahn\'Qiraj (Outside)',509,-8429.74,1512.14,31.9074,0,1122);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4010,'Ahn\'Qiraj Temple (Outside)',531,-8231.33,2010.6,129.861,0,1122);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4012,'Ahn\'Qiraj Temple (Inside)',1,-8242.67,1992.06,129.072,0,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4055,'Naxxramas (Exit)',533,3005.87,-3435.01,293.882,0,1123);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4131,'Karazhan, Main (Entrance)',532,-11100,-2003.98,49.8927,0.577268,1129);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4135,'Karazhan, Service (Entrance)',532,-11040.1,-1996.85,94.6837,2.20224,1129);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4145,'The Shattered Halls (Exit)',530,-311.16,3082.1,-3.71,4.92,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4147,'The Blood Furnace (Exit)',530,-303.506,3164.82,31.7425,5.24025,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4149,'Magtheridon\'s Lair (Exit)',530,-313.679,3088.35,-116.502,2.05307,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4150,'Hellfire Ramparts (Entrance)',543,-1355.24,1641.12,68.2491,0.6687,1137);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4151,'The Shattered Halls (Entrance)',540,-40.8716,-19.7538,-13.8065,1.11133,1138);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4152,'The Blood Furnace (Entrance)',542,-3.9967,14.6363,-44.8009,4.88748,1140);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4153,'Magtheridon\'s Lair (Entrance)',544,187.843,35.9232,67.9252,4.79879,2117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4156,'Naxxramas (Entrance)',533,3498.28,-5349.9,144.968,1.31324,1123);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4297,'Hellfire Ramparts (Exit)',530,-360.671,3071.9,-15.0977,5.14274,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4311,'Battle Of Mount Hyjal, Alliance Base (Entrance)',534,4954,-1886.2,1326,0.13,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4312,'Battle Of Mount Hyjal, Horde Base (Entrance)',534,5497.33,-2971.14,1537.63,2.832,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4313,'Battle Of Mount Hyjal, Night Elf Base (Entrance)',534,5152.33,-3364.4,1644.74,6.2,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4319,'Caverns Of Time, Battle Of Mount Hyjal (Entrance) ',534,4259.61,-4233.77,868.199,2.53,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4320,'Caverns Of Time, Black Morass (Entrance)',269,-1496.24,7034.7,32.5619,1.75699,1143);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4321,'Caverns Of Time, Old Hillsbrad Foothills (Entrance)',560,2741.87,1315.25,14.0423,2.96016,1144);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4322,'Caverns Of Time, Black Morass (Exit)',1,-8765.66,-4175,-209.863,5.53463,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4323,'Caverns Of Time, Battle Of Mount Hyjal (Exit)',1,-8177.5,-4183,-168,4.5,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4324,'Caverns Of Time, Old Hillsbrad Foothills (Exit)',1,-8334.98,-4055.32,-207.737,3.27431,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4352,'Outland To Dark Portal',0,-11877.7,-3204.49,-18.49,0.23,1125);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4354,'Dark Portal To Outland',530,-248,956,85,0,1125);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4363,'The Underbog (Entrance)',546,9.71391,-16.2008,-2.75334,5.57082,1145);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4364,'The Steamvault (Entrance)',545,-13.8425,6.7542,-4.2586,0,1145);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4365,'The Slave Pens (Entrance)',547,120.101,-131.957,-0.801547,1.47574,1145);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4366,'The Steamvault (Exit)',530,816.59,6934.67,-80.5446,4.53174,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4367,'The Underbog (Exit)',530,777.089,6763.45,-72.0662,2.72453,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4379,'The Slave Pens (Exit)',530,719.508,6999.34,-73.0743,4.52702,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4386,'Sunstrider Isle to Eastern Plaguelands',0,3476.36,-4493.36,137.49,0,1111);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4397,'Shadow Labyrinth (Exit)',530,-3645.06,4943.62,-101.048,6.27058,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4399,'Auchenai Crypts (Exit)',530,-3361.96,4660.41,-101.048,4.76654,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4401,'Mana Tombs (Exit)',530,-3079.81,4943.04,-101.047,3.16432,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4403,'Sethekk Halls (Exit)',530,-3362.22,5225.77,-101.049,1.62101,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4404,'Auchenai Crypts (Entrance)',558,-21.8975,0.16,-0.1206,0.0353412,1104);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4405,'Mana Tombs (Entrance)',557,0.0191,0.9478,-0.9543,3.03164,1148);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4406,'Sethekk Halls (Entrance)',556,-4.6811,-0.0930796,0.0062,0.0353424,1148);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4407,'Shadow Labyrinth (Entrance)',555,0.488033,-0.215935,-1.12788,3.15888,1150);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4409,'Eastern Plaguelands To Sunstrider Isle',530,6123,-7005,138,5,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4416,'Serpentshrine Cavern (Entrance)',548,2.5343,-0.022318,821.727,0.004512,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4418,'Serpentshrine Cavern (Exit)',530,827.011,6865.47,-63.7844,3.06507,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4436,'Karazhan, Main (Exit)',0,-11112.9,-2005.89,49.3307,4.02516,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4455,'The Mechanar (Exit)',530,3312.09,1331.89,505.559,2.00554,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4457,'The Eye (Exit)',530,3087.31,1373.79,184.643,1.52918,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4459,'The Botanica (Exit)',530,3413.65,1483.32,182.838,2.54432,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4461,'The Arcatraz (Exit)',530,2862.41,1546.09,252.161,0.805457,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4467,'The Botanica (Entrance)',553,40.0395,-28.613,-1.1189,2.35856,1151);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4468,'The Arcatraz (Entrance)',552,-1.23165,0.0143459,-0.204293,0.0157123,1151);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4469,'The Mechanar (Entrance)',554,-28.906,0.680314,-1.81282,0.0345509,1151);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4470,'The Eye (Entrance)',550,-10.8021,-1.15045,-2.42833,6.22821,1129);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4487,'Battle Of Mount Hyjal (Exit)',1,-8177.5,-4183,-168,4.5,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4523,'Socrethar\'s Seat To Mainland',530,4773.76,3451.27,105.15,3.84,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4534,'Gruul\'s Lair (Exit)',530,3549.8,5085.97,2.46332,2.25742,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4535,'Gruul\'s Lair (Entrance)',565,62.7842,35.462,-3.9835,1.41844,2117);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4561,'Invasion Point, Cataclysm (Return Point)',530,-3278.63,2831.31,123.01,1.56,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4562,'Invasion Point, Cataclysm (Return Point)',530,-3278.63,2831.31,123.01,1.56,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4598,'Black Temple (Entrance)',564,96.4462,1002.35,-86.9984,6.15675,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4619,'Black Temple (Exit)',530,-3653.51,317.493,36.1671,6.24941,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4738,'Zul\'Aman (Entrance)',568,120.7,1776,43.46,4.7713,1129);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4739,'Zul\'Aman (Exit)',530,6851.5,-7997.68,192.36,1.56688,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4885,'Magisters\' Terrace (Exit)',530,12884.6,-7336.17,65.48,1.09,0);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4887,'Magisters\' Terrace (Entrance)',585,7.09,-0.45,-2.8,0.05,1130);
insert  into `areatrigger_teleport`(`id`,`name`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`,`condition_id`) values (4889,'Sunwell Plateau (Entrance)',580,1790.65,925.67,15.15,3.1,1130);
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


