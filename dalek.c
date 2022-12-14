/*
  Licence: GPLv3
  Copyright 2022 Ⓒ Valerie Pond
  Dalek
  
  UnrealIRCd integration with Dalek, with integration with WordPress
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/DalekIRC/unreal-compat/blob/main/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/dalek\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"
#define MSG_PRIVATTEMPT "PRIVATTEMPT"
#define MSG_SPRIVMSG "SPRIVMSG"
#define MSG_MAIL "MAIL"
#define MSG_AJOIN "AJOIN"
#define MSG_SUSPEND "SUSPEND"
#define MSG_UNSUSPEND "UNSUSPEND"
#define MSG_CREGISTER "CREGISTER"
#define MSG_CERTFP "CERTFP"
#define MSG_VOTEBAN "VOTEBAN"

#define IsVoteBan(x)	((x)->mode.mode & EXTMODE_VBAN)

/** Declarations */
/* "Logged in from" whois fields */
int loggedinfrom_whois(Client *requester, Client *acptr, NameValuePrioList **list);

/** Voteban mode checking */
int cmode_voteban_is_ok(Client *client, Channel *channel, char mode, const char *para, int type, int what);
void *cmode_voteban_put_param(void *r_in, const char *param);
const char *cmode_voteban_get_param(void *r_in);
const char *cmode_voteban_conv_param(const char *param_in, Client *client, Channel *channel);
void cmode_voteban_free_param(void *r);
void *cmode_voteban_dup_struct(void *r_in);
int cmode_voteban_sjoin_check(Channel *channel, void *ourx, void *theirx);
int transform_channel_voteban(const char *param);

ModuleHeader MOD_HEADER = {
	"third/dalek",
	"1.0.0",
	"Dalek IRC Services integration",
	"Valware",
	"unrealircd-6",
};


Cmode_t EXTMODE_VBAN = 0L; // our mode

/** The struct which holds which number of votes is
 * expected before a user is banned
*/
typedef struct VoteBan VoteBan;
struct VoteBan {
	int num;
};

/** For sending help files (below) to clients */
static void send_help_to_client(Client *client, char **p)
{
	if(IsServer(client))
		return;

	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);

	add_fake_lag(client, 5000);
}


/**
 * Help files
*/
static char *mail_help[] = {
	"***** Mail *****",
	" ",
	"Description:",
	"This command lets you send messages to users who are not currently online.",
	"The destination must be a registered account name.",
	" ",
	"Syntax:",
	"    /MAIL <account> <message>",
	" ",
	"Examples:",
	"    /MAIL Valware Hey! Drop me a message when you get this? Thanks!",
	" ",
	NULL
};
static char *ajoin_help[] = {
	"***** AJoin *****",
	" ",
	"Description:",
	"This command lets you view and change your auto-join list.",
	"Whichever channels you add to your auto-join list, you will be joined to when",
	"you connect with your account.",
	" ",
	"You must be logged in to use this command.",
	" ",
	"Syntax:",
	"    /AJOIN add|del|list [<channel1,channel2>]",
	" ",
	"Examples:",
	"    /AJOIN list",
	"    /AJOIN add #chat,#radio",
	"    /AJOIN del #usa",
	" ",
	NULL
};
static char *suspend_help[] = {
	"***** Suspend *****",
	" ",
	"Description:",
	"This command lets privileged opers suspend user accounts.",
	" ",
	"You must be logged in to use this command.",
	" ",
	"Syntax:",
	"    /SUSPEND <account> [<reason>]",
	" ",
	"Examples:",
	"    /SUSPEND Lamer32",
	"    /SUSPEND WarezBoi Wares is not allowed",
	" ",
	NULL
};
static char *unsuspend_help[] = {
	"***** Unsuspend *****",
	" ",
	"Description:",
	"This command lets privileged opers unsuspend user accounts.",
	" ",
	"You must be logged in to use this command.",
	" ",
	"Syntax:",
	"    /UNSUSPEND <account>",
	" ",
	"Examples:",
	"    /UNSUSPEND Lamer32",
	" ",
	NULL
};
static char *cregister_help[] = {
	"***** CRegister *****",
	" ",
	"Description:",
	"Allows you to register a channel.",
	" ",
	"You must be logged in and have at least ops on the specified",
	"channel to use this command.",
	" ",
	"Syntax:",
	"    /CREGISTER <channel>",
	" ",
	"Examples:",
	"    /CREGISTER #Chat",
	" ",
	NULL
};
static char *certfp_help[] = {
	"***** CertFP *****",
	" ",
	"Description:",
	"Allows you to view and manage your certificate fingerprints list.",
	" ",
	"You must be logged in and be wearing a certificate fingerprint to",
	"use this command.",
	" ",
	"Syntax:",
	"    /CERTFP add|del|list [<certfp>]",
	" ",
	"Examples:",
	"    /CERTFP add",
	"    /CERTFP del 8e8fb90324550d3a379c75c50e9e3cd52d1d21723532daa4b85b61dc84f48d7d",
	"    /CERTFP list",
	" ",
	NULL
};
static char *voteban_help[] = {
	"***** Voteban *****",
	" ",
	"Description:",
	"Allows users in channels with mode +y <number> to vote on banning a user.",
	" ",
	"Channel ops can set this on channels, where <number> represents",
	"the number of people who must vote to ban a user before they get banned.",
	" ",
	" ",
	"Command syntax:",
	"    /VOTEBAN <channel> <user> [<reason>]",
	"    Note: The reason is optional.",
	" ",
	"Examples:",
	"    /VOTEBAN #PossumsOnly Valware",
	"    /VOTEBAN #PossumsOnly alice for being too cute",
	" ",
	"Mode syntax:",
	"    /MODE <channel> +y <number>",
	"\"<number>\" represents the amount of people who should vote",
	"    before the target is banned.",
	" ",
	"Example:",
	"    /MODE #PossumsOnly +y 5",
	"This has made it so that 5 people must vote in order to ban a",
	"user from a channel.",
	NULL
};
/** Command Override for HELP/HELPOP
 * Shows our commands help in the /HELP and /HELPOP display
*/
CMD_OVERRIDE_FUNC(helpop_ovr)
{
	if (!parv[1])
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	if (!strcasecmp(parv[1], "suspend"))
		send_help_to_client(client, suspend_help);
	
	else if (!strcasecmp(parv[1], "unsuspend"))
		send_help_to_client(client, unsuspend_help);

	else if (!strcasecmp(parv[1],"ajoin"))
		send_help_to_client(client, ajoin_help);

	else if (!strcasecmp(parv[1],"mail"))
		send_help_to_client(client, mail_help);

	else if (!strcasecmp(parv[1], "cregister"))
		send_help_to_client(client, cregister_help);
	
	else if (!strcasecmp(parv[1],"certfp"))
		send_help_to_client(client, certfp_help);
	
	else if (!strcasecmp(parv[1],"voteban"))
		send_help_to_client(client, voteban_help);
	else
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
}

/* Command Definitions */
CMD_OVERRIDE_FUNC(privmsg_ovr);
CMD_FUNC(cmd_privattempt);
CMD_FUNC(cmd_sprivmsg);
CMD_FUNC(cmd_mail);
CMD_FUNC(cmd_ajoin);
CMD_FUNC(cmd_suspend);
CMD_FUNC(cmd_unsuspend);
CMD_FUNC(cmd_cregister);
CMD_FUNC(cmd_certfp);
CMD_FUNC(cmd_voteban);

MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	CmodeInfo creq;

	memset(&creq, 0, sizeof(creq));
	creq.paracount = 1;
	creq.is_ok = cmode_voteban_is_ok;
	creq.letter = 'y';
	creq.put_param = cmode_voteban_put_param;
	creq.get_param = cmode_voteban_get_param;
	creq.conv_param = cmode_voteban_conv_param;
	creq.free_param = cmode_voteban_free_param;
	creq.dup_struct = cmode_voteban_dup_struct;
	creq.sjoin_check = cmode_voteban_sjoin_check;
	CmodeAdd(modinfo->handle, creq, &EXTMODE_VBAN);

	CommandAdd(modinfo->handle, MSG_PRIVATTEMPT, cmd_privattempt, 2, CMD_SERVER|CMD_USER);
	CommandAdd(modinfo->handle, MSG_SPRIVMSG, cmd_sprivmsg, 3, CMD_SERVER);
	CommandAdd(modinfo->handle, MSG_MAIL, cmd_mail, 2, CMD_SERVER|CMD_USER);
	CommandAdd(modinfo->handle, MSG_AJOIN, cmd_ajoin, 2, CMD_USER);
	CommandAdd(modinfo->handle, MSG_SUSPEND, cmd_suspend, 2, CMD_OPER);
	CommandAdd(modinfo->handle, MSG_UNSUSPEND, cmd_unsuspend, 1, CMD_OPER);
	CommandAdd(modinfo->handle, MSG_CREGISTER, cmd_cregister, 1, CMD_USER);
	CommandAdd(modinfo->handle, MSG_CERTFP, cmd_certfp, 2, CMD_USER);
	CommandAdd(modinfo->handle, MSG_VOTEBAN, cmd_voteban, 3, CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_WHOIS, 0, loggedinfrom_whois);
	return MOD_SUCCESS;
}
/** Called upon module load */
MOD_LOAD()
{
	CommandOverrideAdd(modinfo->handle, "PRIVMSG", 0, privmsg_ovr);
	CommandOverrideAdd(modinfo->handle, "HELPOP", 0, helpop_ovr);
	CommandOverrideAdd(modinfo->handle, "HELP", 0, helpop_ovr);
	return MOD_SUCCESS;
}

/** Called upon unload */
MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

/**
 * Command override for PRIVMSG
 * If the user is not online, notify dalek about it if they're online.
 * Dalek will check if it's a registered user, and if so, offer the user
 * to use another command /MESSAGE which will let the user send an offline message to the user.
 * Note: This privmsg interception does NOT forward the message. It only notifies Dalek with
 * three peices of information:
 * - The fact you tried to use PRIVMSG
 * - The sender
 * - The recipient
 * Dalek will only recieve this notification if the target is offline or does not exist.
 * 
 * Below, parv[1] references the target nick
*/
CMD_OVERRIDE_FUNC(privmsg_ovr)
{
	Client *target, *services;

	if (!IsLoggedIn(client) || !(services = find_server(iConf.services_name, NULL))) // no potential services
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	
	if (parc < 2 || *parv[1] == '\0') // Didn't send anything!
	{
		sendnumeric(client, ERR_NORECIPIENT, ovr->command->cmd);
		return;
	}
	if (strstr(parv[1],",") || *parv[1] == '#') // we don't deal with multiple targets or channels, let original function deal with it
	{
	   	CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	if ((target = find_user(parv[1], NULL))) // client is online, don't touch it, let original function deal with it
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
	    return;
	}
	/* So the user is not online. This is where we come in.
	 * At this point we've already confirmed our services is online and that the user the client
	 * is trying to message is offline. So, ask services to let them know about it.
	*/
	sendto_one(services, recv_mtags, ":%s %s %s %s", me.name, MSG_PRIVATTEMPT, client->id, parv[1]);
	
	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // allow it to process normally and tell the user it wasn't online =]
	return;
}

/**
 * S2S command to notify services about a PRIVMSG attempt.
 * @param parv[1] From
 * @param parv[2] To
*/
CMD_FUNC(cmd_privattempt)
{
	/* We checked before, but make sure services is still online as it gets passed down the line... */
	Client *services = find_server(iConf.services_name, NULL);
	if (!services)
	{
		unreal_log(ULOG_INFO, "privattempt", "PRIVATTEMPT_COMMAND", client, "PRIVATTEMPT: $client sent PRIVATTEMPT but we had no services to forward to.");
		return;
	}
	/* It's not for us, it's for Dalek - forward it =] */
	sendto_server(client, 0, 0, recv_mtags, ":%s %s %s %s", client->id, MSG_PRIVATTEMPT, parv[1], parv[2]);
	return;
}

/**
 * Allows services to send a backlog of MAIL messages as if they were
 * natively sent by the person via PRIVMSG. Takes into account sent @time and other mtags.
 * parv[1] = client
 * parv[2] = target
 * parv[3] = :message
*/
CMD_FUNC(cmd_sprivmsg)
{
	if (!IsULine(client))
		return;

	Client *target;

	if (parc < 4) // invalid
		return;

	if (!(target = find_user(parv[2], NULL)))
		return;

	if (!MyUser(target))
	{
		sendto_one(target->uplink, recv_mtags, ":%s SPRIVMSG %s %s :%s", client->id, parv[1], target->name, parv[3]);
		return;
	}
	sendto_one(target, recv_mtags, ":%s PRIVMSG %s :%s", parv[1], target->name, parv[3]);
}

/**
 * Lets users send messages to users who are offline, for when they are online.
 * @param parv[1] Target account or "-list"
 * @param parv[2] Full message
*/
CMD_FUNC(cmd_mail)
{
	Client *services = find_server(iConf.services_name, NULL);
	if (MyUser(client)) // only check if it's being issued on this server
	{
		if (!IsLoggedIn(client))
		{
			sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_MAIL);
			return;
		}
		if (!services)
		{
			sendnumeric(client, ERR_SERVICESDOWN, MSG_MAIL);
			return;
		}

		if (parv[1] && !strcasecmp(parv[1],"-list"))
		{
			sendto_one(services, recv_mtags, ":%s %s %s :", client->id, MSG_MAIL, parv[1]);
			return;
		}
		if (parc < 3)
		{
			send_help_to_client(client, mail_help);
			return;
		}
		Client *target = find_user(parv[1], NULL);
		if (target && IsLoggedIn(target) && !strcasecmp(target->user->account, target->name))
		{
			/* Target online, send to them instead */
			sendto_one(client, NULL, "WARN MAIL MESSAGE_REDIRECTED %s :A user is online with that account. Sent as a normal message.", target->name);
			do_cmd(client, recv_mtags, "PRIVMSG", 3, parv);
			sendto_one(client, recv_mtags, ":%s!%s@%s PRIVMSG %s :%s", target->name, target->user->username, (target->umodes & UMODE_SETHOST) ? target->user->virthost : target->user->cloakedhost, client->name, "[*** Redirected ***]");
			return;
		}
	}
	else if (parc == 3)
		sendto_one(services, recv_mtags, ":%s %s %s :%s", client->id, MSG_MAIL, parv[1], parv[2]);
	return;
}


/**
 * In this whois hook, we let the user see their own "places they are logged in from".
 * Opers can also see this information.
*/
int loggedinfrom_whois(Client *requester, Client *acptr, NameValuePrioList **list)
{
	Client *client;
	char buf[512];
	
	if (!IsOper(requester) && acptr != requester) // only show to the self
		return 0;
	
	int i = 1;
	list_for_each_entry(client, &client_list, client_node)
	{
		if (!strcasecmp(client->user->account,acptr->user->account))
		{
			add_nvplist_numeric_fmt(list, 999900 + i, "loggedin", acptr, 320, "%s :is logged in from %s!%s@%s (%i)%s", acptr->name, client->name, client->user->username, client->user->realhost, i, (client == requester) ? " (You)" : "");
			++i;
		}
	}
	if (acptr->user->account)
	{
		add_nvplist_numeric_fmt(list, 999900, "loggedin", acptr, 320, "%s :is logged in from \x02%i place%s\x02:", acptr->name, i - 1, (i-1 == 1) ? "" : "s");
	}
	
	return 0;
}

/** AJOIN command
 * View and change your auto-join list
 */
CMD_FUNC(cmd_ajoin)
{
	Channel *channel;
	Client *services = find_server(iConf.services_name, NULL);
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_AJOIN);
		return;
	}
	if (!MyUser(client) && parc == 3)
	{
		sendto_one(services, recv_mtags, ":%s AJOIN %s :%s", client->id, parv[1], parv[2]);
		return;
	}
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_AJOIN);
		return;
	}
	if (!parv[1])
	{
		send_help_to_client(client, ajoin_help);
		return;
	}
	if (strcasecmp(parv[1],"add") && strcasecmp(parv[1],"del") && strcasecmp(parv[1],"list"))
	{
		send_help_to_client(client, ajoin_help);
		return;
	}

	if (!strcasecmp(parv[1],"list"))
	{
		sendto_one(services, recv_mtags, ":%s AJOIN LIST :", client->id);
		return;
	}
	else if (!parv[2])
	{
		send_help_to_client(client, ajoin_help);
		return;
	}
	
	if (!(channel = find_channel(parv[2])))
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[2]);
		return;
	}
	sendto_one(services, recv_mtags, ":%s AJOIN %s :%s", client->id, parv[1], parv[2]);

}

/** Suspend command 
 * Suspend account by name
*/
CMD_FUNC(cmd_suspend)
{
	Client *services = find_server(iConf.services_name, NULL);
	if (!ValidatePermissionsForPath("services:can_suspend", client, NULL, NULL, NULL)) // validate with operclasses instead of services-side ;D
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_SUSPEND);
		return;
	}
	if (!MyUser(client) && parc > 2)
	{
		sendto_one(services, recv_mtags, ":%s %s %s :%s", client->id, MSG_SUSPEND, parv[1], parv[2]);
		return;
	}
	
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_SUSPEND);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_SUSPEND);
		return;
	}
	if (!parv[1])
	{
		send_help_to_client(client, suspend_help);
		return;
	}
	if (!parv[2])
	{
		parv[2] = "No reason";
		parv[3] = NULL;
	}
	
	sendto_one(services, recv_mtags, ":%s SUSPEND %s :%s", client->id, parv[1], parv[2]);

}

/** Unsuspend command
 * Unsuspend an account by name
 */
CMD_FUNC(cmd_unsuspend)
{
	Client *services = find_server(iConf.services_name, NULL);
	if (!ValidatePermissionsForPath("services:can_suspend", client, NULL, NULL, NULL)) // validate with operclasses instead of services-side ;D
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_SUSPEND);
		return;
	}
	if (!MyUser(client) && parc > 2)
	{
		sendto_one(services, recv_mtags, ":%s %s %s :%s", client->id, MSG_UNSUSPEND, parv[1], parv[2]);
		return;
	}
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_UNSUSPEND);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_UNSUSPEND);
		return;
	}
	if (!parv[1])
	{
		send_help_to_client(client, unsuspend_help);
		return;
	}
	
	sendto_one(services, recv_mtags, ":%s UNSUSPEND %s", client->id, parv[1]);

}

/** CRegister command
 * Register channels
 */
CMD_FUNC(cmd_cregister)
{
	Client *services = find_server(iConf.services_name, NULL);
	Channel *channel;
	if (!MyUser(client) && parc == 2)
	{
		sendto_one(services, recv_mtags, ":%s CREGISTER %s", client->id, parv[1]);
		return;
	}
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_CREGISTER);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_CREGISTER);
		return;
	}
	if (!parv[1])
	{
		send_help_to_client(client, cregister_help);
		return;
	}
	if (!strcasecmp(parv[1],"help"))
	{
		send_help_to_client(client, cregister_help);
		return;
	}
	else if (!(channel = find_channel(parv[1])))
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (!IsMember(client, channel))
	{
		sendnumeric(client, ERR_NOTONCHANNEL, parv[1]);
		return;
	}
	if (!check_channel_access(client, channel, "oaq"))
	{
		sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
		return;
	}
	if (has_channel_mode(channel, 'r'))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, MSG_CREGISTER, "That channel is already registered.");
		return;
	}
	sendto_one(services, recv_mtags, ":%s CREGISTER %s", client->id, parv[1]);
}


/** CertFP command
 * View or manage your saved Certificate Fingerprint list
 */
CMD_FUNC(cmd_certfp)
{
	Client *services = find_server(iConf.services_name, NULL);
	if (!MyUser(client) && parc == 3)
	{
		sendto_one(services, recv_mtags, ":%s CERTFP %s :%s", client->id, parv[1], parv[2]);
		return;
	}
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_CERTFP);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_CERTFP);
		return;
	}
	if (!parv[1])
	{
		send_help_to_client(client, certfp_help);
		return;
	}
	if (!strcasecmp(parv[1],"help"))
	{
		send_help_to_client(client, certfp_help);
		return;
	}
	if (!strcasecmp(parv[1],"add"))
	{
		ModDataInfo *moddata;
		moddata = findmoddata_byname("certfp", MODDATATYPE_CLIENT);
		if (!moddata || !moddata_client(client, moddata).str)
		{
			sendnumeric(client, ERR_CANNOTDOCOMMAND, MSG_CERTFP, "You do not currently have a certificate fingerprint.");
			return;
		}
		// just let services know they'd like to add it. they will take it from there.
		sendto_one(services, recv_mtags, ":%s CERTFP ADD :", client->id);
		return;
	}
	else if (!strcasecmp(parv[1],"list"))
		sendto_one(services, recv_mtags, ":%s CERTFP LIST :", client->id);
	
	else if (!strcasecmp(parv[1],"del"))
	{
		if (!parv[2])
		{
			send_help_to_client(client, certfp_help);
			return;
		}
		sendto_one(services, recv_mtags, ":%s CERTFP DEL :%s", client->id, parv[2]);
	}
	else
		send_help_to_client(client, certfp_help);
}


int cmode_voteban_is_ok(Client *client, Channel *channel, char mode, const char *param, int type, int what)
{
	if ((type == EXCHK_ACCESS) || (type == EXCHK_ACCESS_ERR))
	{
		/* Permitted for +oaq */
		if (IsUser(client) && check_channel_access(client, channel, "oaq"))
			return EX_ALLOW;
		return EX_DENY;
	} else
	if (type == EXCHK_PARAM)
	{
		/* Actually any value is valid, we just morph it */
		return EX_ALLOW;
	}

	/* fallthrough -- should not be used */
	return EX_DENY;
}
void *cmode_voteban_put_param(void *k_in, const char *param)
{
	VoteBan *fld = (VoteBan *)k_in;

	if (!fld)
		fld = safe_alloc(sizeof(VoteBan));

	fld->num = transform_channel_voteban(param);

	return fld;
}
const char *cmode_voteban_get_param(void *r_in)
{
	VoteBan *r = (VoteBan *)r_in;
	static char retbuf[32];

	if (!r)
		return NULL;

	snprintf(retbuf, sizeof(retbuf), "%d", r->num);
	return retbuf;
}
const char *cmode_voteban_conv_param(const char *param, Client *client, Channel *channel)
{
	static char retbuf[32];
	int v = transform_channel_voteban(param);
	snprintf(retbuf, sizeof(retbuf), "%d", v);
	return retbuf;
}
void cmode_voteban_free_param(void *r)
{
	safe_free(r);
}
void *cmode_voteban_dup_struct(void *r_in)
{
	VoteBan *r = (VoteBan *)r_in;
	VoteBan *w = safe_alloc(sizeof(VoteBan));

	memcpy(w, r, sizeof(VoteBan));

	return (void *)w;
}
int cmode_voteban_sjoin_check(Channel *channel, void *ourx, void *theirx)
{
	VoteBan *our = (VoteBan *)ourx;
	VoteBan *their = (VoteBan *)theirx;

	if (our->num == their->num)
		return EXSJ_SAME;
	else if (our->num > their->num)
		return EXSJ_WEWON;
	else
		return EXSJ_THEYWON;
}

/** Channel voteban param limits: minimum is 1 and maximum is 100
 *  This number represents the amount of votes it takes to ban someone
*/
int transform_channel_voteban(const char *param)
{
	int v = atoi(param);
	if (v <= 0)
		v = 1;
	if (v > 100)
		v = 10;
	return v;
}


CMD_FUNC(cmd_voteban)
{
	Client *target;
	Channel *channel;
	Client *services = find_server(iConf.services_name, NULL);
	if (!MyUser(client) && parc == 4)
	{
		sendto_one(services, recv_mtags, ":%s VOTEBAN %s %s :%s", client->id, parv[1], parv[2], parv[3]);
		return;
	}
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_NEEDREGGEDNICK, MSG_VOTEBAN);
		return;
	}
	if (!services)
	{
		sendnumeric(client, ERR_SERVICESDOWN, MSG_VOTEBAN);
		return;
	}
	if (!parv[1])
	{
		send_help_to_client(client, voteban_help);
		return;
	}
	if (!strcasecmp(parv[1],"help") || parc < 3)
	{
		send_help_to_client(client, voteban_help);
		return;
	}
	if (!(channel = find_channel(parv[1])))
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (!IsVoteBan(channel))
	{
		sendto_one(client, NULL, "FAIL VOTEBAN * :That channel is not accepting ban votes");
		return;
	}
	if (!(target = find_user(parv[2], NULL)))
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[2]);
		return;
	}
	if (!IsMember(client, channel))
	{
		sendnumeric(client, ERR_NOTONCHANNEL, channel->name);
		return;
	}
	if (!IsMember(target, channel))
	{
		sendnumeric(client, ERR_USERNOTINCHANNEL, target->name, channel->name);
		return;
	}
	if (check_channel_access(client, channel, "oaq"))
	{
		sendto_one(client, NULL, "FAIL VOTEBAN YOU_ARE_OPPED :You cannot vote to ban as you already have the ability to ban.");
		return;
	}
	if (check_channel_access(target, channel, "hoaq"))
	{
		sendto_one(client, NULL, "FAIL VOTEBAN CANNOT_VOTE_FOR_OPS :You cannot vote to ban channel operators.");
		return;
	}
	if (IsOper(target))
	{
		sendto_one(client, NULL, "FAIL VOTEBAN CANNOT_VOTE_FOR_OPS :You cannot vote to ban network staff.");
		return;
	}
	
	/* finally if we made it this far */
	sendto_one(services, recv_mtags, ":%s VOTEBAN %s %s :%s", client->id, channel->name, target->id, (parv[3]) ? parv[3] : "No reason");
	add_fake_lag(client, 2000);

}

