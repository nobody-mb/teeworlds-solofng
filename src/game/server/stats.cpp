/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "string.h"
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include "gamecontext.h"
#include <game/gamecore.h>
#include "stats.h"
#include <string.h>
#include <stdio.h>

#include "player.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

tstats::tstats (CGameContext *game_srv, const char *stats_dir)
{
	game_server = game_srv;
	stat_dir = stats_dir;
	
	m_pServer = game_srv->Server();
	
	memset(round_stats, 0, sizeof(round_stats));
	memset(round_names, 0, sizeof(round_names));
	
	num_totals = 0;
	
	struct dirent *ds;
	DIR *dp;
	
	if ((dp = opendir(stats_dir))) {
		while ((ds = readdir(dp)))
			++num_totals;
		closedir(dp);
	} else {
		printf("error reading stats\n");
	}
	
	max_totals = num_totals + 256;
	num_totals = 0;
	
	total_stats = (struct tee_stats *)calloc(max_totals, sizeof(struct tee_stats));
	total_names = (char **)calloc(max_totals, sizeof(char *));
	
	for (int i = 0; i < 512; i++)
		round_names[i] = (char *)calloc(sizeof(char), 64);
	
	if ((dp = opendir(stats_dir))) {
		while ((ds = readdir(dp)))
			if (*ds->d_name != '.') {
				total_stats[num_totals] = read_statsfile(ds->d_name, 0);
				total_names[num_totals] = strdup(ds->d_name);
				num_totals++;
			}
		closedir(dp);
	}
}

tstats::~tstats()
{
	for (int i = 0; i < num_totals; i++)
		free(total_names[i]);
		
	free(total_names);
	free(total_stats);

	for (int i = 0; i < 512; i++)
		free(round_names[i]);
}

void tstats::on_enter (const char *name)
{
	struct tee_stats ts;
	memset(&ts, 0, sizeof(ts));
	ts.join_time = time(NULL);
	add_round_entry(ts, name);
}

void tstats::SendChat(int ChatterClientID, int Team, const char *pText)
{
	game_server->SendChat(ChatterClientID, Team, pText);
}

void tstats::SendChatTarget(int To, const char *pText)
{
	game_server->SendChatTarget(To, pText);
}


void tstats::on_drop (int ClientID, const char *pReason)
{
	struct tee_stats *t;
	
	if ((t = find_round_entry(ID_NAME(ClientID)))) {
		t->spree = 0;	/* thanks SP | Someone :D */
		t->num_games++;
	}
	
	if (pReason && *pReason) {
		char entry[256] = { 0 };
		char aIP[16] = { 0 };
		int fd, len;
	
		Server()->GetClientAddr(ClientID, aIP, sizeof(aIP));
		
		snprintf(entry, sizeof(entry), "%s left (%s) %s\n", ID_NAME(ClientID), 
			pReason, aIP);
		len = (int)strlen(entry);
			
		printf("%s", entry);
			
		if ((fd = open("leaving.txt", O_RDWR|O_CREAT|O_APPEND, 0777)) < 0)
			perror("open");
		else
			if (write(fd, entry, len) != len)
				perror("write");
			
		close(fd);
	}
}

double tstats::print_best_group (char *dst, struct tee_stats *stats, char **names, 
	int num, double (*callback)(struct tee_stats, char *), double max)
{
	int i, len;
	double kd = 0, best = 0;
	char tmp_buf[256] = { 0 };
	char call_buf[128] = { 0 };
	
	for (i = 0; i < num; i++) {
		if (!names[i][0])
			continue;
		kd = callback(stats[i], call_buf);
		if ((kd > best) && (kd < max))
			best = kd;
	}
	for (i = 0; i < num; i++) {
		if (!names[i][0])
			continue;
		memset(call_buf, 0, sizeof(call_buf));
		kd = callback(stats[i], call_buf);
		if (kd == best) {
			len = strlen(call_buf) + strlen(tmp_buf) + strlen(names[i]) + 5;
			if (len >= sizeof(tmp_buf))
				break;
			strcat(tmp_buf, names[i]);
			if (strlen(call_buf)) {
				strcat(tmp_buf, " (");
				strcat(tmp_buf, call_buf);
				strcat(tmp_buf, ")");
			}
			strcat(tmp_buf, ", ");
		}
	}
	
	tmp_buf[strlen(tmp_buf) - 2] = 0;

	sprintf(dst, "%.03f (%s)", best, ((best != 0) ? tmp_buf : "None"));
	
	return best;
}

#define PLACEHOLDER 9999999999

void tstats::print_best (int max, double (*callback)(struct tee_stats, char *), int all)
{
	double tmp, best = PLACEHOLDER;
	char buf[256];

	while (max--) {
		memset(buf, 0, sizeof(buf));
		if (all)
			tmp = print_best_group(buf, total_stats, total_names, 
				num_totals, callback, best);
		else
			tmp = print_best_group(buf, round_stats, round_names, 
				round_index, callback, best);
		if ((tmp >= best) || (best < PLACEHOLDER && tmp == 0))
			break;
		SendChat(-1, CGameContext::CHAT_ALL, buf);
		best = tmp;
		if (best == 0) 
			break;
	}
}

void tstats::send_stats (const char *name, int req_by, struct tee_stats *ct)
{
	char buf[256];
	int c, d;
	time_t diff;
	
	/* no ones going to play this long */
	if ((time(NULL) - ct->join_time) < (60 * 60 * 24))
		diff = time(NULL) - ct->join_time;
	else
		diff = ct->join_time;

	str_format(buf, sizeof(buf), "stats for %s (requested by %s)", 
		name, Server()->ClientName(req_by));
	SendChat(-1, CGameContext::CHAT_ALL, buf);

	d = ct->deaths ? ct->deaths : 1;
	c = ct->kills + ct->kills_x2 + ct->kills_wrong;	
	str_format(buf, sizeof(buf), 
		"- kills: %d (%d x2, %d wrong) | deaths: %d | ratio: %.03f",
		c, ct->kills_x2, ct->kills_wrong, ct->deaths, (float)c / (float)d);
	SendChat(-1, CGameContext::CHAT_ALL, buf);
	
	str_format(buf, sizeof(buf), "- freezes: %d | frozen: %d | bounce shots: %d", 
		ct->freezes, ct->frozen, ct->bounce_shots);
	SendChat(-1, CGameContext::CHAT_ALL, buf);
				
	c = ct->shots ? ct->shots : 1; 
	d = ct->frozen ? ct->frozen : 1;
	str_format(buf, sizeof(buf),
		"- avg ping: %d | shots: %d | accuracy: %.03f%%", ct->avg_ping, ct->shots, 
		ct->freezes, 100.0f * ((float)ct->freezes / (float)c));
	SendChat(-1, CGameContext::CHAT_ALL, buf);
			
	str_format(buf, sizeof(buf), 
		"- hammers: %d | hammered: %d | steals: %d | suicides: %d",
		ct->hammers, ct->hammered, ct->steals, ct->suicides);
	SendChat(-1, CGameContext::CHAT_ALL, buf);
				
	str_format(buf, sizeof(buf), "- time: %d:%.02d | max spree: %d | multis (max %d): %d:",
		diff / 60, diff % 60, ct->spree_max, ct->max_multi, ct->multis[0] + 
		ct->multis[1] + ct->multis[2] + ct->multis[3] + ct->multis[4] + ct->multis[5]);
	SendChat(-1, CGameContext::CHAT_ALL, buf);

/* ranks! */

	if (ct->multis[0]) {
		str_format(buf, sizeof(buf), "- ** double kills: %d", ct->multis[0]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[1]) {
		str_format(buf, sizeof(buf), "- ** triple kills: %d", ct->multis[1]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[2]) {
		str_format(buf, sizeof(buf), "- ** quad kills: %d", ct->multis[2]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[3]) {
		str_format(buf, sizeof(buf), "- ** penta kills: %d", ct->multis[3]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[4]) {
		str_format(buf, sizeof(buf), "- ** ultra kills: %d", ct->multis[4]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	} if (ct->multis[5]) {
		str_format(buf, sizeof(buf), "- ** god kills: %d", ct->multis[5]);
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	}
	
	/*if (ct->is_bot) {
		str_format(buf, sizeof(buf), "note: a player with this name has triggered the automatic aimbot detector");
		SendChat(-1, CGameContext::CHAT_ALL, buf);
	}*/
}
struct tee_stats tstats::read_statsfile (const char *name, time_t create)
{
	char path[128];
	int src_fd;
	struct tee_stats ret;
	
	memset(&ret, 0, sizeof(ret));
	
	snprintf(path, sizeof(path), "%s/%s", stat_dir, name);
	if ((src_fd = open(path, O_RDWR, 0777)) < 0) {
		if (create) {
			fprintf(stderr, "creating file\n");
			if ((src_fd = open(path, O_WRONLY|O_CREAT, 0777)) < 0) {
				fprintf(stderr, "error creating file %s\n", path);
				return ret;
			}
			ret.join_time = 0;//create;
			write(src_fd, &ret, sizeof(ret));
			
			total_stats[num_totals].join_time = 0;//create;
			total_names[num_totals] = strdup(name);
			
			if (++num_totals >= max_totals) {
				max_totals += 256;
				total_stats = (struct tee_stats *)realloc(total_stats,
					max_totals * sizeof(struct tee_stats));
				total_names = (char **)realloc(total_names, 
					max_totals * sizeof(char *));
			}
		}
	} else {
		if (read(src_fd, &ret, sizeof(ret)) != sizeof(ret)) {
			fprintf(stderr, "didnt read enough data\n");
		}
	}
	close(src_fd);
	
	ret.join_time = 0;

	return ret;
}


double tstats::get_max_spree (struct tee_stats fstats, char *buf)
{
	return (double)fstats.spree_max;
}

double tstats::get_steals (struct tee_stats fstats, char *buf)
{
	int k = fstats.kills + fstats.kills_x2 + fstats.kills_wrong;
	if (k && buf)
		sprintf(buf, "%.02f%%", 
			((double)fstats.steals / (double)k) * 100);
		
	return (double)fstats.steals;
}

double tstats::get_kd (struct tee_stats fstats, char *buf)
{
	int k = fstats.kills + fstats.kills_x2 + fstats.kills_wrong;
	int d = fstats.deaths ? fstats.deaths : 1;
	return (double)k / (double)d;
}

double tstats::get_kills (struct tee_stats fstats, char *buf)
{
	return (double)(fstats.kills + fstats.kills_x2 + fstats.kills_wrong);
}

double tstats::get_hammers (struct tee_stats fstats, char *buf)
{
	return (double)fstats.hammers;
}

double tstats::get_accuracy (struct tee_stats fstats, char *buf)
{
	if (fstats.shots < 2)
		return 0.0f;
		
	if (buf)
		sprintf(buf, "%d ping", fstats.avg_ping);
		
	int d = fstats.shots ? fstats.shots : 1;
	return (double)fstats.freezes / (double)d;
}

double tstats::get_bounces (struct tee_stats fstats, char *buf)
{
	return (double)fstats.bounce_shots;
}


void tstats::update_stats (struct tee_stats *dst, struct tee_stats *src)
{
	if (!dst || !src)
		return;
		
	//if (!dst->join_time)
	//	dst->join_time = time(NULL);
	
	if (src->spree_max > dst->spree_max)
		dst->spree_max = src->spree_max;
	if (src->max_multi > dst->max_multi)
		dst->max_multi = src->max_multi;
	
	for (int i = 0; i < 6; i++)
		dst->multis[i] += src->multis[i];
		
	dst->kills += src->kills;
	dst->kills_x2 += src->kills_x2;
	dst->kills_wrong += src->kills_wrong;
	dst->deaths += src->deaths;
	dst->steals += src->steals;
	dst->suicides += src->suicides;
	dst->shots += src->shots;
	dst->freezes += src->freezes;
	dst->frozen += src->frozen;
	dst->hammers += src->hammers;
	dst->hammered += src->hammered;
	dst->teamhooks += src->teamhooks;
	dst->bounce_shots += src->bounce_shots;
	if (src->is_bot)
		dst->is_bot += 1;
	dst->join_time += (time(NULL) - src->join_time);
	dst->avg_ping = (unsigned short)((float)(src->avg_ping + 
					(float)(dst->num_samples * 
					dst->avg_ping)) / 
					(++dst->num_samples));
}

void tstats::on_round_end (void)
{
	int i, j, src_fd;
	struct tee_stats totals;
	char path[128];
	
	SendChat(-1, CGameContext::CHAT_ALL, "most steals:");
	print_best(4, &get_steals, 0);
	
	SendChat(-1, CGameContext::CHAT_ALL, "best spree:");
	print_best(4, &get_max_spree, 0);
	
	SendChat(-1, CGameContext::CHAT_ALL, "best k/d:");
	print_best(4, &get_kd, 0);
	
	SendChat(-1, CGameContext::CHAT_ALL, "best accuracy:");
	print_best(4, &get_accuracy, 0);
	
	for (i = 0; i < round_index; i++) {
		if (!round_names[i][0])
			continue;
		memset(&totals, 0, sizeof(totals));
		for (j = 0; j < num_totals; j++) {
			if (!strncmp(round_names[i], total_names[j], 
			    strlen(round_names[i])))
				break;
		}
		if (j == num_totals) {
			//++num_totals;
			total_stats[j] = read_statsfile(round_names[i], time(NULL));
		}

		update_stats(&total_stats[j], &round_stats[i]);			
						
		snprintf(path, sizeof(path), "%s/%s", stat_dir, round_names[i]);
		if ((src_fd = open(path, O_RDWR, 0777)) < 0) {
			fprintf(stderr, "creating file\n");
			if ((src_fd = open(path, O_WRONLY|O_CREAT, 0777)) < 0) {
				fprintf(stderr, "error creating %s\n", path);
				perror("a");
				continue;	
			}
		}
	
		write(src_fd, &total_stats[j], sizeof(struct tee_stats));
		close(src_fd);
	}
	memset(round_stats, 0, sizeof(round_stats));
	for (i = 0; i < 512; i++)
		memset(round_names[i], 0, 64);
	round_index = 0;
	printf("round ended !\n");
	
	for (i = 0; i < MAX_CLIENTS; i++) {
		memset(&totals, 0, sizeof(totals));
		totals.join_time = time(NULL);
		if (!m_apPlayers[i])
			continue;
		add_round_entry(totals, ID_NAME(m_apPlayers[i]->GetCID()));
	}
}

struct tee_stats *tstats::find_round_entry (const char *name)
{
	int i;
	
	for (i = 0; i < 512; i++)
		if (!strncmp(name, round_names[i], strlen(name)))
			return &round_stats[i];
			
	return NULL;
}

struct tee_stats *tstats::add_round_entry (struct tee_stats st, const char *name)
{
	int i;
	
	for (i = 0; i < 512; i++)
		if (!strncmp(name, round_names[i], strlen(name)))
			break;
	if (i == 512)
		i = round_index++;
	if (i >= 512) {
		printf("exceeded max round player entries!\n");
		return NULL;
	}
	
	printf("adding round entry for %s (%d)\n", name, i);
	
	strcpy(round_names[i], name);
	
	update_stats(&round_stats[i], &st);
					
	return &round_stats[i];
}

void tstats::on_msg (const char *message, int ClientID)
{
	printf("[cmd msg] %s: %s\n", ID_NAME(ClientID), message);
	
	if (str_comp_num(message, "/statsall", 9) == 0) {
		if (strlen(message) > 10) {
			char namebuf[64] = { 0 };
			strcpy(namebuf, message + 10);
			char *ptr = namebuf + strlen(namebuf) - 1;
			if (*ptr == ' ')
				*ptr = 0;
			struct tee_stats tmp;
			
			tmp = read_statsfile(namebuf, 0);
			if (!tmp.shots) {										
				SendChatTarget(ClientID, "invalid player");
				printf("invalid player %s\n", namebuf);
			} else {
				send_stats(namebuf, ClientID, &tmp);
			}
		} else {
			struct tee_stats tmp;
			tmp = read_statsfile(ID_NAME(ClientID), 0);
			send_stats(ID_NAME(ClientID), ClientID, &tmp);
		}
	} else if (str_comp_num(message, "/stats", 6) == 0) {
		if (strlen(message) > 7) {
			char namebuf[64] = { 0 };
			strcpy(namebuf, message + 7);
			char *ptr = namebuf + strlen(namebuf) - 1;
			if (*ptr == ' ')
				*ptr = 0;
			struct tee_stats *tmp;
			tmp = find_round_entry(namebuf);
			if (!tmp) {
				SendChatTarget(ClientID, "invalid player");
				printf("invalid player %s\n", namebuf);
			} else {
				send_stats(namebuf, ClientID, tmp);
			}
		} else {
			struct tee_stats *tmp;
			tmp = find_round_entry(ID_NAME(ClientID));
			if (tmp)
				send_stats(ID_NAME(ClientID), ClientID, tmp);
		}
	} else if (str_comp_num(message, "/topkills", 9) == 0) {
		SendChat(-1, CGameContext::CHAT_ALL, "most kills:");
		print_best(12, &get_kills, 1);
	} else if (str_comp_num(message, "/topsteals", 9) == 0) {
		SendChat(-1, CGameContext::CHAT_ALL, "most steals:");
		print_best(12, &get_steals, 1);
	} else if (str_comp_num(message, "/top", 4) == 0) { 
		int all = 0;
		if (str_comp_num(message, "/topall", 7) == 0) {
			char mg[128] = { 0 };
			snprintf(mg, sizeof(mg), "all-time stats req by %s", 
			Server()->ClientName(ClientID));
			SendChat(-1, CGameContext::CHAT_ALL, mg);
			if (((int)time(NULL) - last_reqd) < 5) {
				SendChatTarget(ClientID, "stop spam");
				return;
			}
			last_reqd = (int)time(NULL);
			all = 1;
		}

		SendChat(-1, CGameContext::CHAT_ALL, "most steals:");
		print_best(4, &get_steals, all);

		SendChat(-1, CGameContext::CHAT_ALL, "most bounces:");
		print_best(4, &get_bounces, all);
		
		SendChat(-1, CGameContext::CHAT_ALL, "best spree:");
		print_best(4, &get_max_spree, all);
		
		if (all) {
			SendChat(-1, CGameContext::CHAT_ALL, "most hammers:");
			print_best(4, &get_hammers, all);		
		
			SendChat(-1, CGameContext::CHAT_ALL, "most kills:");
			print_best(4, &get_kills, all);
		} else {
			SendChat(-1, CGameContext::CHAT_ALL, "best k/d:");
			print_best(4, &get_kd, all);		
		
			SendChat(-1, CGameContext::CHAT_ALL, "best accuracy:");
			print_best(4, &get_accuracy, all);
		}
	} 
}
